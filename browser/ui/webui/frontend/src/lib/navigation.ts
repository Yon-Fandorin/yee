import { page } from '$app/state';

export function currentPageURL() {
	return page.shallow?.url ?? page.url;
}
