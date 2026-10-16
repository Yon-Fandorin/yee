<script lang="ts">
	import Puzzle from '@lucide/svelte/icons/puzzle';
	let { src, large = false }: { src?: string; large?: boolean } = $props();
	let failed = $state(false);
	$effect(() => {
		src;
		failed = false;
	});
</script>

<span class="extension-icon" class:large>
	{#if src && !failed}<img {src} alt="" onerror={() => (failed = true)} />
	{:else}<Puzzle size={18} strokeWidth={1.5} aria-hidden="true" />{/if}
</span>

<style>
	@layer components {
		.extension-icon {
			display: grid;
			place-items: center;
			width: 30px;
			height: 30px;
			flex: none;
			color: var(--muted-foreground);
		}
		img {
			width: 24px;
			height: 24px;
			object-fit: contain;
		}
		.large {
			width: 42px;
			height: 42px;
		}
		.large img {
			width: 36px;
			height: 36px;
		}
	}
</style>
