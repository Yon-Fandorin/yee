<script lang="ts">
	import { mergeProps } from 'bits-ui';
	import Search from '@lucide/svelte/icons/search';
	import X from '@lucide/svelte/icons/x';
	import { Button } from '#lib/components/ui/button/index.ts';
	import { Input } from '#lib/components/ui/input/index.ts';
	import Hint from './Hint.svelte';
	let {
		value,
		label,
		clearLabel,
		oninput
	}: {
		value: string;
		label: string;
		clearLabel: string;
		oninput: (value: string) => void;
	} = $props();
</script>

<div class="search-field" role="search">
	<Search size={15} aria-hidden="true" />
	<Input
		type="search"
		{value}
		oninput={(event) => oninput(event.currentTarget.value)}
		placeholder={label}
		aria-label={label}
		class="border-0 bg-transparent pl-8 shadow-none"
	/>
	{#if value}<Hint text={clearLabel}>
			{#snippet children({ props })}
				<Button
					{...mergeProps(props, { onclick: () => oninput('') })}
					size="icon-sm"
					variant="ghost"
					class="clear-search"
					aria-label={clearLabel}
				>
					<X size={14} />
				</Button>
			{/snippet}
		</Hint>{/if}
</div>

<style>
	@layer components {
		.search-field {
			position: relative;
			width: min(100%, 340px);
		}
		.search-field > :global(svg) {
			position: absolute;
			inset-inline-start: 9px;
			top: 6px;
			pointer-events: none;
			color: var(--muted-foreground);
		}
		.search-field :global(input) {
			padding-inline-end: 30px;
		}
		.search-field :global(input::-webkit-search-cancel-button) {
			appearance: none;
		}
		.search-field :global(.clear-search) {
			position: absolute;
			inset-inline-end: 2px;
			top: 2px;
		}
		@media (max-width: 620px) {
			.search-field {
				width: 100%;
			}
		}
	}
</style>
