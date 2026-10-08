import type { Reroute } from '@sveltejs/kit/hooks';

export const reroute: Reroute = ({ url }) => {
	if (url.hostname === 'yee-downloads') return '/downloads';
};
