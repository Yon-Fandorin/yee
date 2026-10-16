<script lang="ts">
	// Establish Tailwind's layer order before importing component styles.
	import '#styles';
	import type { Snippet } from 'svelte';
	import { onMount } from 'svelte';
	import { observeThemeAccent } from '#lib/theme.ts';
	import { Provider as TooltipProvider } from '#lib/components/ui/tooltip/index.ts';
	let { children }: { children: Snippet } = $props();
	onMount(observeThemeAccent);
</script>

<TooltipProvider delayDuration={400}>
	{@render children()}
</TooltipProvider>
