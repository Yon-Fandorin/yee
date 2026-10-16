<script lang="ts">
	import type { Snippet } from 'svelte';
	import Icon, { type IconName } from '#lib/components/Icon.svelte';
	let {
		icon,
		title,
		description,
		variant = 'grouped',
		children
	}: {
		icon?: IconName;
		title: string;
		description?: string;
		variant?: 'grouped' | 'plain';
		children?: Snippet;
	} = $props();
</script>

<div class="settings-row" class:plain={variant === 'plain'}>
	{#if icon}<span class="row-icon"><Icon name={icon} /></span>{/if}
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
		.settings-row.plain {
			min-height: 0;
			padding: 18px 0;
			gap: 28px;
			border-bottom: 1px solid var(--border);
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
			line-height: 1.6;
		}
		@media (max-width: 540px) {
			.settings-row {
				padding: 13px 12px;
				gap: 10px;
			}
			.settings-row.plain {
				flex-wrap: wrap;
				gap: 12px;
			}
		}
	}
</style>
