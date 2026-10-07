import {
	addWebUiListener,
	removeWebUiListener,
	sendWithPromise
} from 'chrome://resources/js/cr.js';

export interface BlockedDomain {
	domain: string;
	includeSubdomains: boolean;
}

export interface DomainImportPreview {
	rows: (BlockedDomain & {
		line: number;
		status: 'add' | 'duplicate' | 'invalid-domain' | 'invalid-columns' | 'invalid-scope';
	})[];
	additions: number;
	duplicates: number;
	invalid: number;
}

export type DomainImportFormat = 'csv' | 'txt';

export interface ContentBlockingState {
	enabled: boolean;
	privateProfile: boolean;
	exceptions: string[];
	blockedDomains: BlockedDomain[];
	updatesAvailable: boolean;
	updating: boolean;
	runningDownloaded: boolean;
	pendingRestart: boolean;
	checkedAt: number;
	updateSucceeded?: boolean;
}

export const settingsBridge = {
	getState: () => sendWithPromise<ContentBlockingState>('getContentBlockingState'),
	setException: (input: string, enabled: boolean) =>
		sendWithPromise<ContentBlockingState>('setContentBlockingException', input, enabled),
	checkLists: () => sendWithPromise<ContentBlockingState>('checkContentBlockingLists'),
	addDomain: (domain: string, includeSubdomains: boolean) =>
		sendWithPromise<ContentBlockingState>('addBlockedDomain', domain, includeSubdomains),
	setDomain: (domain: string, includeSubdomains: boolean) =>
		sendWithPromise<ContentBlockingState>('setBlockedDomain', domain, includeSubdomains),
	removeDomain: (domain: string) =>
		sendWithPromise<ContentBlockingState>('removeBlockedDomain', domain),
	previewDomains: (format: DomainImportFormat, text: string) =>
		sendWithPromise<DomainImportPreview>('previewBlockedDomains', format, text),
	importDomains: (format: DomainImportFormat, text: string) =>
		sendWithPromise<number>('importBlockedDomains', format, text),
	subscribe(callback: () => void): () => void {
		const listener = addWebUiListener('content-blocking-settings-changed', callback);
		return () => {
			removeWebUiListener(listener);
		};
	}
};
