import { loadTimeData } from 'chrome://resources/js/load_time_data.js';
import { t } from '#lib/i18n.ts';
import type {
	Data,
	PageCallbackRouter,
	PageHandlerFactoryRemote,
	PageHandlerRemote,
	State
} from './downloads.mojom-webui.js';

export type DownloadAction = 'open' | 'show' | 'pause' | 'resume' | 'cancel' | 'retry' | 'remove';

export class DownloadsModel {
	items = $state<Data[]>([]);
	states = $state<typeof State | null>(null);
	query = $state('');
	loading = $state(true);
	more = $state(true);
	error = $state('');
	removed = $state(false);
	undoable = $state(false);
	readonly allowDeletingHistory = loadTimeData.getBoolean('allowDeletingHistory');
	private handler: PageHandlerRemote | null = null;
	private router: PageCallbackRouter | null = null;
	private factory: PageHandlerFactoryRemote | null = null;
	private timer: ReturnType<typeof setTimeout> | undefined;
	private terms: string[] | null = null;
	private disposed = false;

	constructor(query = '') {
		this.query = query;
	}

	async connect() {
		try {
			const moduleUrl = '/downloads.mojom-webui.js';
			const native = (await import(
				/* @vite-ignore */ moduleUrl
			)) as typeof import('./downloads.mojom-webui.js');
			if (this.disposed) return;
			this.states = native.State;
			const router = (this.router = new native.PageCallbackRouter());
			const handler = (this.handler = new native.PageHandlerRemote());
			this.factory = native.PageHandlerFactory.getRemote();
			for (const endpoint of [router, handler, this.factory]) {
				endpoint.onConnectionError.addListener(() => this.fail());
			}
			router.insertItems.addListener((index: number, items: Data[]) => {
				this.items.splice(index, 0, ...items);
				this.loading = false;
				this.more = items.length > 0;
			});
			router.updateItem.addListener((index: number, item: Data) => {
				this.items[index] = item;
			});
			router.removeItem.addListener((index: number) => this.items.splice(index, 1));
			router.clearAll.addListener(() => {
				this.items = [];
				this.more = true;
			});
			this.factory.createPageHandler(
				router.$.bindNewPipeAndPassRemote(),
				handler.$.bindNewPipeAndPassReceiver()
			);
			this.search();
		} catch {
			if (!this.disposed) this.fail();
		}
	}

	setQuery(value: string) {
		this.query = value;
		clearTimeout(this.timer);
		this.timer = setTimeout(() => this.search(), 200);
	}

	private search() {
		if (!this.handler || this.error) return;
		const terms = this.query
			.split(/"([^"]*)"/)
			.map((term) => term.trim())
			.filter(Boolean);
		if (JSON.stringify(terms) === JSON.stringify(this.terms)) return;
		this.terms = terms;
		this.more = true;
		this.loadMore();
	}

	loadMore() {
		if (!this.handler || this.error) return;
		this.loading = true;
		this.handler.getDownloads(this.terms ?? []);
	}

	act(action: DownloadAction, item: Data) {
		const handler = this.handler;
		if (!handler || this.error) return;
		switch (action) {
			case 'open':
				handler.openFileRequiringGesture(item.id);
				break;
			case 'show':
				handler.show(item.id);
				break;
			case 'pause':
				handler.pause(item.id);
				break;
			case 'resume':
				handler.resume(item.id);
				break;
			case 'cancel':
				handler.cancel(item.id);
				break;
			case 'retry':
				handler.retryDownload(item.id);
				break;
			case 'remove':
				if (!this.allowDeletingHistory) return;
				handler.remove(item.id);
				this.removed = true;
				this.undoable = true;
		}
	}

	clear() {
		if (!this.handler || this.error || !this.allowDeletingHistory || this.query) return;
		clearTimeout(this.timer);
		this.undoable = this.items.some((item) => !item.isDangerous && !item.isInsecure);
		this.loading = true;
		this.handler.clearAll();
		this.removed = true;
	}

	undo() {
		if (!this.undoable || this.error) return;
		this.handler?.undo();
		this.removed = false;
	}

	openFolder() {
		this.handler?.openDownloadsFolderRequiringGesture();
	}

	private fail() {
		if (this.disposed) return;
		this.loading = false;
		this.error = t('downloadsLoadFailed');
	}

	dispose() {
		this.disposed = true;
		clearTimeout(this.timer);
		this.router?.$.close();
		this.handler?.$.close();
		this.factory?.$.close();
	}
}
