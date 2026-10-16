<script lang="ts">
	import Ellipsis from '@lucide/svelte/icons/ellipsis';
	import TriangleAlert from '@lucide/svelte/icons/triangle-alert';
	import { Button } from '#lib/components/ui/button/index.ts';
	import { Switch } from '#lib/components/ui/switch/index.ts';
	import * as DropdownMenu from '#lib/components/ui/dropdown-menu/index.ts';
	import Hint from '#lib/components/Hint.svelte';
	import { t } from '#lib/i18n.ts';
	import {
		displayState,
		enabled,
		warningMessages,
		type ExtensionInfo,
		type ExtensionsModel
	} from '../extensions.svelte.ts';
	import ExtensionIcon from './ExtensionIcon.svelte';
	let {
		item,
		model,
		detailsURL,
		ondetails
	}: { item: ExtensionInfo; model: ExtensionsModel; detailsURL: string; ondetails: () => void } =
		$props();
	const warnings = $derived(warningMessages(item));
</script>

<li class="extension-row" data-extension-id={item.id}>
	<ExtensionIcon src={item.iconUrl} />
	<div class="summary">
		<Button
			href={detailsURL}
			variant="ghost"
			class="extension-name"
			data-extension-name
			onclick={(event) => {
				if (event.button || event.metaKey || event.ctrlKey || event.shiftKey || event.altKey)
					return;
				event.preventDefault();
				ondetails();
			}}>{item.name}</Button
		>
		<p class="description">{item.description || t('extensionsVersion', item.version)}</p>
		{#if warnings.length}<span class="warning"
				><TriangleAlert size={12} strokeWidth={1.5} aria-hidden="true" />{warnings[0]}</span
			>{/if}
	</div>
	<span class="state">{displayState(item)}</span>
	<Hint text={t('extensionsToggle', item.name)}>
		{#snippet children({ props })}
			<Switch
				{...props}
				checked={enabled(item)}
				disabled={!!model.busy || !model.canToggle(item)}
				onCheckedChange={(value) => model.setEnabled(item, value)}
				aria-label={t('extensionsToggle', item.name)}
				data-extension-enable
				data-extension-toggle
			/>
		{/snippet}
	</Hint>
	<DropdownMenu.Root>
		<Hint text={t('extensionsActions', item.name)}>
			{#snippet children({ props: tooltipProps })}
				<DropdownMenu.Trigger {...tooltipProps}>
					{#snippet child({ props })}<Button
							{...props}
							variant="ghost"
							size="icon"
							aria-label={t('extensionsActions', item.name)}
							><Ellipsis size={16} strokeWidth={1.5} /></Button
						>{/snippet}
				</DropdownMenu.Trigger>
			{/snippet}
		</Hint>
		<DropdownMenu.Content align="end">
			<DropdownMenu.Item onclick={ondetails}>{t('extensionsDetails')}</DropdownMenu.Item>
			{#if item.optionsPage}<DropdownMenu.Item
					disabled={!!model.busy || item.state !== 'ENABLED'}
					onclick={() => model.openOptions(item)}>{t('extensionsOptions')}</DropdownMenu.Item
				>{/if}
			<DropdownMenu.Item
				onclick={() => {
					location.href = model.nativeURL(item);
				}}>{t('extensionsNativeDetails')}</DropdownMenu.Item
			>
			{#if !item.mustRemainInstalled}
				<DropdownMenu.Separator />
				<DropdownMenu.Item
					variant="destructive"
					disabled={!!model.busy}
					onclick={() => model.remove(item)}>{t('extensionsRemove')}</DropdownMenu.Item
				>
			{/if}
		</DropdownMenu.Content>
	</DropdownMenu.Root>
</li>

<style>
	@layer components {
		.extension-row {
			display: flex;
			align-items: center;
			gap: 14px;
			padding: 14px 8px;
			min-height: 72px;
			border-bottom: 1px solid var(--border);
		}
		.extension-row:hover {
			background: color-mix(in oklch, var(--muted), transparent 60%);
		}
		.summary {
			flex: 1;
			min-width: 0;
		}
		.summary :global(.extension-name) {
			display: block;
			max-width: 100%;
			height: auto;
			padding: 0;
			font-size: 13px;
			font-weight: 500;
			text-align: start;
			white-space: nowrap;
			overflow: hidden;
			text-overflow: ellipsis;
			background: transparent;
		}
		.description {
			margin: 3px 0 0;
			color: var(--muted-foreground);
			font-size: 12px;
			white-space: nowrap;
			overflow: hidden;
			text-overflow: ellipsis;
		}
		.state {
			font-size: 11px;
			color: var(--muted-foreground);
		}
		.warning {
			display: flex;
			align-items: center;
			gap: 5px;
			margin-top: 5px;
			font-size: 11px;
			color: var(--destructive);
		}
		@media (max-width: 620px) {
			.extension-row {
				gap: 10px;
			}
			.state {
				display: none;
			}
		}
	}
</style>
