<script lang="ts">
	import type { Snippet } from 'svelte';
	import Icon, { type IconName } from '#lib/components/Icon.svelte';
	let {
		icon,
		title,
		description,
		children
	}: {
		icon: IconName;
		title: string;
		description?: string;
		children?: Snippet;
	} = $props();
</script>

<div class="settings-row">
	<span class="row-icon"><Icon name={icon} /></span>
	<div class="row-label">
		<strong>{title}</strong>
		{#if description}<span>{description}</span>{/if}
	</div>
	{@render children?.()}
</div>

<style>
	@layer components {
		.settings-row {
			display: flex;
			align-items: center;
			gap: 12px;
			min-height: 64px;
			padding: 14px 16px;
		}
		.row-icon {
			display: grid;
			place-items: center;
			width: 30px;
			height: 30px;
			flex: none;
			background: var(--accent);
			color: var(--muted-foreground);
			border-radius: 6px;
		}
		.row-label {
			display: flex;
			flex-direction: column;
			gap: 3px;
			min-width: 0;
			flex: 1;
		}
		strong {
			font-size: 13px;
			font-weight: 500;
		}
		.row-label > span {
			color: var(--muted-foreground);
			font-size: 12px;
		}
		@media (max-width: 540px) {
			.settings-row {
				padding: 13px 12px;
				gap: 10px;
			}
		}
	}
</style>
