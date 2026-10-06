import type { ClientInit } from '@sveltejs/kit/hooks';
import { version } from '$app/env';

export const init: ClientInit = () => {
	if (location.protocol !== 'chrome:') return;

	// Settings resources ship with the browser. SvelteKit's focus checks use
	// fetch(), which cannot load WebUI schemes; serve this immutable version.
	const versionUrl = new URL('/_app/version.json', location.href);
	const nativeFetch = window.fetch;
	window.fetch = async (input, options) => {
		const request = input instanceof Request ? input : undefined;
		const url = new URL(request ? request.url : input.toString(), location.href);
		const method = (options?.method ?? request?.method ?? 'GET').toUpperCase();
		if (url.href === versionUrl.href && method === 'GET') {
			(options?.signal ?? request?.signal)?.throwIfAborted();
			return Response.json({ version });
		}
		return nativeFetch.call(window, input, options);
	};
};
