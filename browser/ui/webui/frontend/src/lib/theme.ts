import {
	addWebUiListener,
	removeWebUiListener,
	sendWithPromise
} from 'chrome://resources/js/cr.js';

export function observeThemeAccent(): () => void {
	let active = true;
	let revision = 0;
	const apply = ({ accent, container }: { accent: string; container: string }) => {
		if (!active) return;
		const root = document.documentElement;
		root.toggleAttribute(
			'data-theme-accent',
			/^#[0-9a-f]{6}$/i.test(accent) && /^#[0-9a-f]{6}$/i.test(container)
		);
		if (root.hasAttribute('data-theme-accent')) {
			root.style.setProperty('--yee-theme-accent', accent);
			root.style.setProperty('--yee-theme-container', container);
		} else {
			root.style.removeProperty('--yee-theme-accent');
			root.style.removeProperty('--yee-theme-container');
		}
	};
	const listener = addWebUiListener(
		'yee-theme-accent-changed',
		(colors: { accent: string; container: string }) => {
			revision++;
			apply(colors);
		}
	);
	// 초기 응답보다 먼저 온 테마 이벤트의 최신 값을 유지한다.
	void sendWithPromise<{ accent: string; container: string }>('getYeeThemeAccent').then(
		(colors) => {
			if (revision === 0) apply(colors);
		}
	);
	return () => {
		active = false;
		removeWebUiListener(listener);
	};
}
