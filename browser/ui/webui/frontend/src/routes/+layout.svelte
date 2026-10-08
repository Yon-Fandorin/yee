<script lang="ts">
	// Establish Tailwind's layer order before importing component styles.
	import '#styles';
	import type { Snippet } from 'svelte';
	import { Provider as TooltipProvider } from '#lib/components/ui/tooltip/index.ts';
	let { children }: { children: Snippet } = $props();
</script>

<TooltipProvider delayDuration={400}>
	{@render children()}
</TooltipProvider>
