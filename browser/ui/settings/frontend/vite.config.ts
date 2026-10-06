import adapter from '@sveltejs/adapter-static';
import { sveltekit } from '@sveltejs/kit/vite';
import { defineConfig } from 'vite';
import tailwindcss from '@tailwindcss/vite';

export default defineConfig({
	plugins: [
		tailwindcss(),
		sveltekit({
			adapter: adapter({ fallback: 'index.html' }),
			compilerOptions: { fragments: 'tree' },
			output: { bundleStrategy: 'single' },
			paths: { relative: false },
			serviceWorker: { register: false },
			version: { pollInterval: 0 }
		})
	],
	build: {
		target: 'es2022',
		rolldownOptions: { external: (id) => id.startsWith('chrome://') }
	}
});
