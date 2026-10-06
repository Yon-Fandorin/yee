import { onMount, tick } from 'svelte';
import { page } from '$app/state';
import { afterNavigate, snapshot } from '$app/navigation';

export function manageSettingsScroll(
	getContainer: () => HTMLElement | null,
	isReady: () => boolean
): void {
	let restoredScroll = $state<number | null>(null);
	onMount(() => {
		const scrollKey = 'settings:content-scroll';
		try {
			const saved = JSON.parse(sessionStorage.getItem(scrollKey) ?? 'null');
			sessionStorage.removeItem(scrollKey);
			if (saved?.url === location.href && Number.isFinite(saved.top) && saved.top >= 0) {
				restoredScroll = saved.top;
			}
		} catch {}
		// Client-only startup does not restore the router snapshot on a full reload.
		const saveScroll = () => {
			try {
				sessionStorage.setItem(
					scrollKey,
					JSON.stringify({ url: location.href, top: getContainer()?.scrollTop ?? 0 })
				);
			} catch {}
		};
		window.addEventListener('pagehide', saveScroll);
		return () => window.removeEventListener('pagehide', saveScroll);
	});
	afterNavigate(({ type }) => {
		if (type !== 'enter') getContainer()?.focus({ preventScroll: true });
	});
	snapshot({
		id: 'settings-content-scroll',
		capture: () => getContainer()?.scrollTop ?? 0,
		restore: (top) => {
			restoredScroll = top;
		},
		reset: () => {
			restoredScroll = null;
			if (!page.url.hash) getContainer()?.scrollTo({ top: 0 });
		}
	});
	// Reloaded site exceptions must finish loading before restoring their position.
	$effect(() => {
		const container = getContainer();
		const top = restoredScroll;
		if (container && top !== null && isReady()) {
			void tick().then(() => {
				if (getContainer() !== container || restoredScroll !== top) return;
				container.scrollTop = top;
				restoredScroll = null;
			});
		}
	});
}
