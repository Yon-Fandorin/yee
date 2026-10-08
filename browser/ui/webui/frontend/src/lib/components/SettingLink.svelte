<script lang="ts">
	import Icon, { type IconName } from '#lib/components/Icon.svelte';
	import SettingsRow from '#lib/components/SettingsRow.svelte';
	let {
		href,
		icon,
		title,
		description,
		value = '',
		external = false
	}: {
		href: string;
		icon: IconName;
		title: string;
		description?: string;
		value?: string;
		external?: boolean;
	} = $props();
</script>

<a {href} class="setting-link">
	<SettingsRow {icon} {title} {description}>
		{#if value}<span class="row-value">{value}</span>{/if}
		<span class="row-chevron"><Icon name={external ? 'external' : 'arrow'} size={14} /></span>
	</SettingsRow>
</a>

<style>
	@layer components {
		.setting-link {
			display: block;
		}
		:global(.setting-link) + .setting-link {
			border-top: 1px solid var(--border);
		}
		.setting-link:hover {
			background: var(--muted);
		}
		.row-value {
			color: var(--muted-foreground);
			font-size: 12px;
			text-align: end;
			max-width: 36%;
		}
		.row-chevron {
			display: flex;
			color: var(--muted-foreground);
			flex: none;
		}
		:global(:root[dir='rtl']) .row-chevron {
			transform: scaleX(-1);
		}
		@media (max-width: 780px) {
			.row-value {
				max-width: 30%;
			}
		}
	}
</style>
