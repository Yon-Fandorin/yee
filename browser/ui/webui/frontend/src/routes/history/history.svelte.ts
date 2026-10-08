import { loadTimeData } from 'chrome://resources/js/load_time_data.js';
import { SvelteSet } from 'svelte/reactivity';
import { t } from '#lib/i18n.ts';
import type { HistoryEntry, PageCallbackRouter, PageHandlerRemote } from './history.mojom-webui.js';

export function entryKey(item: HistoryEntry): string {
	return `${item.time}:${item.url}`;
}

export class HistoryModel {
	items = $state<HistoryEntry[]>([]);
	query = $state('');
	loading = $state(true);
	more = $state(false);
	error = $state('');
	notice = $state('');
	otherForms = $state(false);
	deleting = $state(false);
	pendingRemoval = $state<HistoryEntry[]>([]);
	readonly selected = new SvelteSet<string>();
	readonly allowDeletingHistory = loadTimeData.getBoolean('allowDeletingHistory');
	readonly activityUrl = loadTimeData.getString('historyActivityUrl');
	allSelected = $derived(
		this.items.length > 0 && this.items.every((item) => this.selected.has(entryKey(item)))
	);
	private handler: PageHandlerRemote | null = null;
	private router: PageCallbackRouter | null = null;
	private timer: ReturnType<typeof setTimeout> | undefined;
	private revision = 0;
	private disposed = false;

	constructor(query = '') {
		this.query = query;
	}

	async connect() {
		try {
			const moduleUrl = 'chrome://resources/cr_components/history/history.mojom-webui.js';
			const native = (await import(
				/* @vite-ignore */ moduleUrl
			)) as typeof import('./history.mojom-webui.js');
			if (this.disposed) return;
			this.router = new native.PageCallbackRouter();
			this.handler = native.PageHandler.getRemote();
			this.router.onConnectionError.addListener(() => this.fail());
			this.handler.onConnectionError.addListener(() => this.fail());
			this.router.onHistoryDeleted.addListener(() => {
				void this.refresh();
			});
			this.router.onHasOtherFormsChanged.addListener((hasOtherForms) => {
				this.otherForms = hasOtherForms;
			});
			this.handler.setPage(this.router.$.bindNewPipeAndPassRemote());
			await this.refresh();
		} catch {
			if (!this.disposed) this.fail();
		}
	}

	setQuery(value: string) {
		if (value === this.query) return;
		this.query = value;
		++this.revision;
		this.loading = true;
		this.selected.clear();
		this.notice = '';
		clearTimeout(this.timer);
		this.timer = setTimeout(() => {
			void this.refresh();
		}, 200);
	}

	async refresh() {
		this.selected.clear();
		await this.fetch(false);
	}

	async loadMore() {
		if (this.loading || !this.more) return;
		await this.fetch(true);
	}

	private async fetch(incremental: boolean) {
		if (!this.handler || this.disposed || this.error) return;
		const revision = ++this.revision;
		this.loading = true;
		try {
			const { results } = await (incremental
				? this.handler.queryHistoryContinuation()
				: this.handler.queryHistory(this.query, 150, null, true, true));
			if (this.disposed || revision !== this.revision) return;
			if (!results.info) {
				this.fail();
				return;
			}
			this.items = incremental ? [...this.items, ...results.value] : results.value;
			this.more = !results.info.finished;
			this.loading = false;
		} catch {
			if (!this.disposed && revision === this.revision) this.fail();
		}
	}

	toggle(item: HistoryEntry, checked: boolean) {
		if (!this.allowDeletingHistory || this.deleting) return;
		if (checked) this.selected.add(entryKey(item));
		else this.selected.delete(entryKey(item));
	}

	selectAll(checked: boolean) {
		if (!this.allowDeletingHistory || this.deleting) return;
		this.selected.clear();
		if (checked) for (const item of this.items) this.selected.add(entryKey(item));
	}

	requestRemoval(items = this.items.filter((item) => this.selected.has(entryKey(item)))) {
		if (!this.allowDeletingHistory || this.loading || this.deleting || this.error) return;
		this.pendingRemoval = [...items];
	}

	cancelRemoval() {
		if (!this.deleting) this.pendingRemoval = [];
	}

	async confirmRemoval() {
		if (!this.handler || !this.allowDeletingHistory || this.deleting || !this.pendingRemoval.length)
			return;
		this.deleting = true;
		try {
			const visits = this.pendingRemoval.flatMap((item) =>
				Object.entries(item.allTimestamps).map(([url, timestamps]) => ({ url, timestamps }))
			);
			await this.handler.removeVisits(visits);
			if (this.disposed) return;
			this.pendingRemoval = [];
			this.notice = t('historyDeleted');
			await this.refresh();
		} catch {
			if (!this.disposed) {
				this.pendingRemoval = [];
				this.notice = t('historyDeleteFailed');
			}
		} finally {
			this.deleting = false;
		}
	}

	open(item: HistoryEntry, event: MouseEvent) {
		if (event.button !== 0 && event.button !== 1) return;
		event.preventDefault();
		chrome.send('navigateToUrl', [
			item.url,
			'',
			event.button,
			event.altKey,
			event.ctrlKey,
			event.metaKey,
			event.shiftKey
		]);
	}

	clearBrowsingData() {
		this.handler?.openClearBrowsingDataDialog();
	}

	private fail() {
		if (this.disposed) return;
		this.loading = false;
		this.error = t('historyLoadFailed');
	}

	dispose() {
		this.disposed = true;
		++this.revision;
		clearTimeout(this.timer);
		this.router?.$.close();
		this.handler?.$.close();
	}
}
