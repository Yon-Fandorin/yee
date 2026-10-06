declare module 'chrome://yee-settings/strings.m.js' {}
declare module '#styles' {}

declare module 'chrome://resources/js/cr.js' {
	export function sendWithPromise<T>(method: string, ...args: unknown[]): Promise<T>;
	export interface WebUiListener {
		eventName: string;
		uid: number;
	}
	export function addWebUiListener(event: string, callback: () => void): WebUiListener;
	export function removeWebUiListener(listener: WebUiListener): boolean;
}
declare module 'chrome://resources/js/load_time_data.js' {
	export const loadTimeData: {
		getString(key: string): string;
		getStringF(key: string, ...args: string[]): string;
	};
}
