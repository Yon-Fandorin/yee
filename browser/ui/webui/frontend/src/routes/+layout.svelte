<script lang="ts">
	// Establish Tailwind's layer order before importing component styles.
	import '#styles';
	import type { Snippet } from 'svelte';
	let { children }: { children: Snippet } = $props();
</script>

{@render children()}
