import {
	addWebUiListener,
	removeWebUiListener,
	sendWithPromise
} from 'chrome://resources/js/cr.js';
import { t } from '#lib/i18n.ts';
import type {} from './native/bookmarks.js';
import type {} from './native/bookmark_manager_private.js';

export type BookmarkNode = chrome.bookmarks.BookmarkTreeNode;
export type Editor =
	| { kind: 'bookmark'; parentId: string }
	| { kind: 'folder'; parentId: string }
	| { kind: 'edit'; id: string }
	| { kind: 'move'; id: string };

export class BookmarksModel {
	roots = $state<BookmarkNode[]>([]);
	nodes = $state<Record<string, BookmarkNode>>({});
	folderId = $state('');
	expanded = $state<string[]>([]);
	query = $state('');
	results = $state<BookmarkNode[]>([]);
	selected = $state<string[]>([]);
	loading = $state(true);
	busy = $state(false);
	error = $state('');
	loadFailed = $state(false);
	canEdit = $state(false);
	editor = $state<Editor | null>(null);
	editorError = $state('');
	pendingRemoval = $state<string[]>([]);
	undoable = $state(false);
	notice = $state('');
	private disposed = false;
	private searchVersion = 0;
	private refreshVersion = 0;
	private searchTimer: ReturnType<typeof setTimeout> | undefined;
	private refreshTimer: ReturnType<typeof setTimeout> | undefined;
	private unsubscribe: (() => void)[] = [];
	constructor(private readonly onNavigate: (id: string, replace?: boolean) => void) {}

	get folder() {
		return this.nodes[this.folderId];
	}

	get hasMultipleStores() {
		return [true, false].every((syncing) =>
			this.roots.some((node) => node.folderType === 'bookmarks-bar' && node.syncing === syncing)
		);
	}

	private get defaultFolderId() {
		return (
			this.roots.find((node) => node.folderType === 'bookmarks-bar' && node.syncing)?.id ??
			this.roots.find((node) => node.folderType === 'bookmarks-bar')?.id ??
			this.roots[0]?.id ??
			''
		);
	}

	get items() {
		return this.query.trim() ? this.results : (this.folder?.children ?? []);
	}

	get breadcrumbs(): BookmarkNode[] {
		const path: BookmarkNode[] = [];
		let node = this.folder;
		while (node?.parentId && node.id !== '0') {
			path.unshift(node);
			node = this.nodes[node.parentId];
		}
		return path;
	}

	get canAdd() {
		return !!this.folder && this.canEdit && !this.isManaged(this.folder) && !this.query.trim();
	}

	isManaged(node: BookmarkNode) {
		while (node) {
			if (node.unmodifiable || node.folderType === 'managed') return true;
			if (!node.parentId) break;
			node = this.nodes[node.parentId];
		}
		return false;
	}

	canModify(node: BookmarkNode) {
		return this.canEdit && !!node.parentId && node.parentId !== '0' && !this.isManaged(node);
	}

	folderPath(node: BookmarkNode): string {
		const titles: string[] = [];
		let current: BookmarkNode | undefined = node;
		while (current?.parentId) {
			titles.unshift(current.title || t('bookmarksUntitled'));
			current = this.nodes[current.parentId];
		}
		if (this.hasMultipleStores && !this.isManaged(node)) {
			titles.unshift(t(node.syncing ? 'bookmarksAccount' : 'bookmarksDevice'));
		}
		return titles.join(' / ');
	}

	get moveDestinations() {
		const moving = this.editor?.kind === 'move' ? this.editor.id : '';
		return Object.values(this.nodes).filter((node) => {
			if (node.url || !node.parentId || this.isManaged(node)) return false;
			let current: BookmarkNode | undefined = node;
			while (current) {
				if (current.id === moving) return false;
				current = current.parentId ? this.nodes[current.parentId] : undefined;
			}
			return true;
		});
	}

	async connect() {
		try {
			if (!chrome.bookmarks || !chrome.bookmarkManagerPrivate) throw new Error('Unavailable');
			const policy = addWebUiListener<[boolean]>('can-edit-bookmarks-changed', (enabled) => {
				this.canEdit = enabled;
				if (!enabled) {
					this.selected = [];
					this.pendingRemoval = [];
					this.editor = null;
					this.undoable = false;
				}
			});
			this.unsubscribe.push(() => removeWebUiListener(policy));
			const changed = () => this.scheduleRefresh();
			for (const event of [
				chrome.bookmarks.onCreated,
				chrome.bookmarks.onRemoved,
				chrome.bookmarks.onChanged,
				chrome.bookmarks.onMoved,
				chrome.bookmarks.onChildrenReordered,
				chrome.bookmarks.onImportEnded
			]) {
				event.addListener(changed);
				this.unsubscribe.push(() => event.removeListener(changed));
			}
			const enabled = await sendWithPromise<boolean>('getCanEditBookmarks');
			if (this.disposed) return;
			this.canEdit = enabled;
			const url = new URL(window.location.href);
			this.folderId = url.searchParams.get('id') ?? '';
			this.query = url.searchParams.get('q') ?? '';
			await this.refresh();
		} catch {
			this.failLoading();
		}
	}

	private scheduleRefresh() {
		clearTimeout(this.refreshTimer);
		this.refreshTimer = setTimeout(() => void this.refresh(), 50);
	}

	async refresh() {
		const version = ++this.refreshVersion;
		try {
			const tree = await chrome.bookmarks.getTree();
			if (this.disposed || version !== this.refreshVersion) return;
			const nodes: Record<string, BookmarkNode> = {};
			const collect = (node: BookmarkNode) => {
				nodes[node.id] = node;
				node.children?.forEach(collect);
			};
			tree.forEach(collect);
			const oldPath = this.breadcrumbs.map((node) => node.id).reverse();
			this.nodes = nodes;
			this.roots = tree[0]?.children ?? [];
			if (!nodes[this.folderId] || nodes[this.folderId].url || this.folderId === '0') {
				this.folderId = oldPath.find((id) => nodes[id] && !nodes[id].url) ?? this.defaultFolderId;
				if (oldPath.length && this.folderId) this.onNavigate(this.folderId, true);
			}
			this.expandAncestors();
			this.selected = this.selected.filter((id) => nodes[id] && this.canModify(nodes[id]));
			this.pendingRemoval = this.pendingRemoval.filter(
				(id) => nodes[id] && this.canModify(nodes[id])
			);
			if (this.editor) {
				const node = nodes['id' in this.editor ? this.editor.id : this.editor.parentId];
				if (!node || this.isManaged(node) || ('id' in this.editor && !this.canModify(node))) {
					this.editor = null;
				}
			}
			this.loadFailed = false;
			this.error = '';
			if (this.query.trim()) await this.search();
			else this.loading = false;
		} catch {
			if (version === this.refreshVersion) this.failLoading();
		}
	}

	navigate(id: string, updateLocation = true) {
		const node = this.nodes[id];
		if (!node || node.url || id === '0') return;
		this.folderId = id;
		this.setQuery('');
		this.selected = [];
		this.expandAncestors();
		if (updateLocation) this.onNavigate(id);
	}

	syncLocation(id: string, query: string) {
		const folder = id || this.defaultFolderId;
		if (!this.roots.length) this.folderId = folder;
		else if (folder !== this.folderId) this.navigate(folder, false);
		if (query !== this.query) this.setQuery(query);
	}

	private expandAncestors() {
		this.expanded = [...new Set([...this.expanded, ...this.breadcrumbs.map((node) => node.id)])];
	}

	toggleFolder(id: string) {
		this.expanded = this.expanded.includes(id)
			? this.expanded.filter((value) => value !== id)
			: [...this.expanded, id];
	}

	setQuery(query: string) {
		this.query = query;
		this.selected = [];
		this.searchVersion++;
		clearTimeout(this.searchTimer);
		if (!query.trim()) {
			this.results = [];
			this.loading = false;
			return;
		}
		this.loading = true;
		this.searchTimer = setTimeout(() => void this.search(), 180);
	}

	private async search() {
		const version = ++this.searchVersion;
		const query = this.query.trim();
		if (!query) return;
		try {
			const results = await chrome.bookmarks.search(query);
			if (this.disposed || version !== this.searchVersion) return;
			this.results = results.map((node) => this.nodes[node.id] ?? node);
			this.loading = false;
			this.loadFailed = false;
			this.error = '';
		} catch {
			if (version === this.searchVersion) this.failLoading();
		}
	}

	toggleSelected(id: string, checked: boolean) {
		const node = this.nodes[id];
		if (!node || !this.canModify(node) || this.busy) return;
		this.selected = checked
			? [...new Set([...this.selected, id])]
			: this.selected.filter((value) => value !== id);
	}

	open(node: BookmarkNode) {
		if (!node.url) this.navigate(node.id);
		else chrome.bookmarkManagerPrivate.openInNewTab(node.id, { active: true, split: false });
	}

	openEditor(editor: Editor) {
		if (this.busy || !this.canEdit) return;
		if ('id' in editor && (!this.nodes[editor.id] || !this.canModify(this.nodes[editor.id])))
			return;
		if ('parentId' in editor && !this.canAdd) return;
		this.editorError = '';
		this.editor = editor;
	}

	cancelEditor() {
		if (!this.busy) this.editor = null;
	}

	async saveEditor(title: string, url: string, destination: string) {
		const editor = this.editor;
		if (!editor || this.busy || !this.canEdit) return;
		this.editorError = '';
		this.busy = true;
		try {
			if (editor.kind === 'move') {
				if (!this.moveDestinations.some((folder) => folder.id === destination)) {
					throw new Error('Invalid destination');
				}
				await chrome.bookmarks.move(editor.id, { parentId: destination });
			} else if (editor.kind === 'edit') {
				const node = this.nodes[editor.id];
				if (!node || !this.canModify(node)) throw new Error('Unavailable');
				await chrome.bookmarks.update(editor.id, { title, ...(node.url ? { url } : {}) });
			} else {
				const parent = this.nodes[editor.parentId];
				if (!parent || this.isManaged(parent)) throw new Error('Unavailable');
				await chrome.bookmarks.create({
					parentId: editor.parentId,
					title,
					...(editor.kind === 'bookmark' ? { url } : {})
				});
			}
			if (this.disposed) return;
			this.editor = null;
			this.notice = t('bookmarksSaved');
			this.undoable = true;
			await this.refresh();
		} catch {
			if (!this.disposed) this.editorError = t('bookmarksSaveFailed');
		} finally {
			this.busy = false;
		}
	}

	requestRemoval(ids: string[]) {
		if (this.busy) return;
		const removable = new Set(ids.filter((id) => this.nodes[id] && this.canModify(this.nodes[id])));
		// Search results can select both a folder and one of its descendants.
		this.pendingRemoval = [...removable].filter((id) => {
			let parent = this.nodes[id]?.parentId;
			while (parent) {
				if (removable.has(parent)) return false;
				parent = this.nodes[parent]?.parentId;
			}
			return true;
		});
		this.error = '';
	}

	cancelRemoval() {
		if (!this.busy) this.pendingRemoval = [];
	}

	async confirmRemoval() {
		const ids = this.pendingRemoval.filter(
			(id) => this.nodes[id] && this.canModify(this.nodes[id])
		);
		if (!ids.length || this.busy) return;
		this.busy = true;
		try {
			await chrome.bookmarkManagerPrivate.removeTrees(ids);
			if (this.disposed) return;
			this.pendingRemoval = [];
			this.selected = [];
			this.notice = t('bookmarksDeleted');
			this.undoable = true;
			await this.refresh();
		} catch {
			if (!this.disposed) this.error = t('bookmarksActionFailed');
		} finally {
			this.busy = false;
		}
	}

	undo() {
		if (!this.undoable || !this.canEdit || this.busy) return;
		chrome.bookmarkManagerPrivate.undo();
		this.undoable = false;
		this.notice = '';
	}

	async transfer(kind: 'import' | 'export') {
		if (this.busy || (kind === 'import' && !this.canEdit)) return;
		this.busy = true;
		try {
			await chrome.bookmarkManagerPrivate[kind]();
		} catch {
			if (!this.disposed) this.error = t('bookmarksActionFailed');
		} finally {
			this.busy = false;
		}
	}

	private failLoading() {
		if (this.disposed) return;
		this.loading = false;
		this.loadFailed = true;
		this.error = t('bookmarksLoadFailed');
	}

	dispose() {
		this.disposed = true;
		this.searchVersion++;
		this.refreshVersion++;
		clearTimeout(this.searchTimer);
		clearTimeout(this.refreshTimer);
		this.unsubscribe.forEach((unsubscribe) => unsubscribe());
	}
}
