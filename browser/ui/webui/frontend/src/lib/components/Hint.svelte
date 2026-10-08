<script lang="ts">
	import type { Tooltip as TooltipPrimitive } from 'bits-ui';
	import * as Tooltip from '#lib/components/ui/tooltip/index.ts';

	let {
		text,
		children
	}: {
		text: string;
		children: NonNullable<TooltipPrimitive.TriggerProps['child']>;
	} = $props();
</script>

<Tooltip.Root>
	<Tooltip.Trigger>
		{#snippet child({ props })}{@render children({ props })}{/snippet}
	</Tooltip.Trigger>
	<Tooltip.Content sideOffset={6} class="break-all shadow-sm motion-reduce:animate-none">
		{text}
	</Tooltip.Content>
</Tooltip.Root>
