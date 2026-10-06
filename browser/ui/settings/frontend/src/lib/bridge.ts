import {
	addWebUiListener,
	removeWebUiListener,
	sendWithPromise
} from 'chrome://resources/js/cr.js';

export interface ContentBlockingState {
	enabled: boolean;
	privateProfile: boolean;
	exceptions: string[];
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
	subscribe(callback: () => void): () => void {
		const listener = addWebUiListener('content-blocking-settings-changed', callback);
		return () => {
			removeWebUiListener(listener);
		};
	}
};
