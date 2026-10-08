<script lang="ts">
	import { mergeProps } from 'bits-ui';
	import X from '@lucide/svelte/icons/x';
	import Star from '@lucide/svelte/icons/star';
	import Globe from '@lucide/svelte/icons/globe';
	import { Button } from '#lib/components/ui/button/index.ts';
	import { Checkbox } from '#lib/components/ui/checkbox/index.ts';
	import { Badge } from '#lib/components/ui/badge/index.ts';
	import Hint from '#lib/components/Hint.svelte';
	import { locale, t } from '#lib/i18n.ts';
	import type { HistoryEntry } from '../history.mojom-webui.js';
	import { entryKey } from '../history.svelte.ts';

	let {
		item,
		selected,
		allowDeletingHistory,
		disabled,
		onselect,
		onremove,
		onopen
	}: {
		item: HistoryEntry;
		selected: boolean;
		allowDeletingHistory: boolean;
		disabled: boolean;
		onselect: (checked: boolean) => void;
		onremove: () => void;
		onopen: (event: MouseEvent) => void;
	} = $props();
	let iconFailed = $state(false);
	const title = $derived(item.title || item.url);
	const timestamp = $derived(
		new Intl.DateTimeFormat(locale, { hour: 'numeric', minute: '2-digit' }).format(item.time)
	);
	const favicon = $derived(
		`chrome://favicon2/?size=16&scaleFactor=${window.devicePixelRatio}x&pageUrl=${encodeURIComponent(item.url)}`
	);
</script>

<li class="history-row" class:selected data-history-id={entryKey(item)}>
	{#if allowDeletingHistory}<Checkbox
			checked={selected}
			{disabled}
			onCheckedChange={onselect}
			aria-label={t('historySelect', title)}
		/>{/if}
	<time datetime={new Date(item.time).toISOString()}>{item.dateTimeOfDay || timestamp}</time>
	<div class="site-icon" aria-hidden="true">
		{#if iconFailed}<Globe size={16} strokeWidth={1.5} />{:else}
			<img
				src={favicon}
				alt=""
				width="16"
				height="16"
				onerror={() => {
					iconFailed = true;
				}}
			/>
		{/if}
	</div>
	<div class="details">
		<Hint text={item.url}>
			{#snippet children({ props })}
				<a
					{...mergeProps(props, { onclick: onopen, onauxclick: onopen })}
					href={item.url}
					class="title">{title}</a
				>
			{/snippet}
		</Hint>
		<div class="metadata">
			<span>{item.domain}</span>
			{#if item.deviceName}<span>{item.deviceName}</span>{/if}
			{#if item.isActorVisit}<span>{t('historyActor')}</span>{/if}
			{#if item.blockedVisit}<Badge variant="outline" class="text-destructive"
					>{t('historyBlocked')}</Badge
				>{/if}
			{#if item.starred}<Hint text={t('historyBookmark')}>
					{#snippet children({ props })}<span
							{...props}
							role="img"
							aria-label={t('historyBookmark')}
						>
							<Star size={12} aria-hidden="true" />
						</span>{/snippet}
				</Hint>{/if}
		</div>
		{#if item.snippet}<p class="snippet">{item.snippet}</p>{/if}
		{#if item.criticalActions.length}<div class="critical-actions">
				{#each item.criticalActions as action (action.id)}
					<Hint text={action.tooltip}>
						{#snippet children({ props })}<Button
								{...props}
								href={action.linkoutUrl}
								variant="outline"
								size="xs"
							>
								{action.label}
							</Button>{/snippet}
					</Hint>
				{/each}
			</div>{/if}
	</div>
	{#if allowDeletingHistory}<Hint text={t('historyRemove')}>
			{#snippet children({ props })}<Button
					{...mergeProps(props, { onclick: onremove })}
					variant="ghost"
					size="icon"
					{disabled}
					aria-label={t('historyRemove')}
				>
					<X size={15} />
				</Button>{/snippet}
		</Hint>{/if}
</li>

<style>
	@layer components {
		.history-row {
			display: flex;
			align-items: center;
			gap: 14px;
			padding: 14px 8px;
			border-bottom: 1px solid var(--border);
		}
		.history-row.selected {
			background: var(--muted);
		}
		time {
			width: 65px;
			flex-shrink: 0;
			font-size: 12px;
			color: var(--muted-foreground);
		}
		.site-icon {
			display: grid;
			place-items: center;
			width: 24px;
			flex-shrink: 0;
			color: var(--muted-foreground);
		}
		.details {
			flex: 1;
			min-width: 0;
		}
		.title {
			display: block;
			width: fit-content;
			max-width: 100%;
			overflow: hidden;
			text-overflow: ellipsis;
			white-space: nowrap;
			font-weight: 500;
		}
		.title:hover {
			text-decoration: underline;
		}
		.metadata {
			display: flex;
			align-items: center;
			flex-wrap: wrap;
			gap: 5px 12px;
			margin-top: 3px;
			font-size: 12px;
			color: var(--muted-foreground);
		}
		.metadata > span:first-child {
			max-width: 30ch;
			overflow: hidden;
			text-overflow: ellipsis;
			white-space: nowrap;
		}
		.snippet {
			margin: 5px 0 0;
			font-size: 12px;
			color: var(--muted-foreground);
		}
		.critical-actions {
			display: flex;
			flex-wrap: wrap;
			gap: 5px;
			margin-top: 7px;
		}
		@media (max-width: 620px) {
			.history-row {
				gap: 10px;
			}
			time {
				width: 56px;
			}
			.site-icon {
				display: none;
			}
		}
	}
</style>
