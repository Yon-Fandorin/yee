declare module '*/strings.m.js' {}
declare module '#styles' {}

declare namespace chrome {
	function send(message: string, args?: unknown[]): void;
}

declare module 'chrome://resources/js/cr.js' {
	export function sendWithPromise<T>(method: string, ...args: unknown[]): Promise<T>;
	export interface WebUiListener {
		eventName: string;
		uid: number;
	}
	export function addWebUiListener<TArgs extends unknown[] = []>(
		event: string,
		callback: (...args: TArgs) => void
	): WebUiListener;
	export function removeWebUiListener(listener: WebUiListener): boolean;
}
declare module 'chrome://resources/js/load_time_data.js' {
	export const loadTimeData: {
		getBoolean(key: string): boolean;
		getInteger(key: string): number;
		getString(key: string): string;
		getStringF(key: string, ...args: string[]): string;
	};
}
