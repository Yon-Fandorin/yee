<script lang="ts">
	import { onMount, untrack } from 'svelte';
	import { page } from '$app/state';
	import { goto } from '$app/navigation';
	import History from '@lucide/svelte/icons/history';
	import { Button } from '#lib/components/ui/button/index.ts';
	import { Checkbox } from '#lib/components/ui/checkbox/index.ts';
	import { Label } from '#lib/components/ui/label/index.ts';
	import InternalPage from '#lib/components/InternalPage.svelte';
	import SearchField from '#lib/components/SearchField.svelte';
	import { t } from '#lib/i18n.ts';
	import HistoryRow from './components/HistoryRow.svelte';
	import HistoryDeletionDialog from './components/HistoryDeletionDialog.svelte';
	import { entryKey, HistoryModel } from './history.svelte.ts';

	const model = new HistoryModel(page.url.searchParams.get('q') ?? '');
	function setQuery(value: string) {
		model.setQuery(value);
		const url = new URL(page.url.href);
		if (value) url.searchParams.set('q', value);
		else url.searchParams.delete('q');
		void goto(url, { shallow: true, replace: true, state: page.state });
	}
	$effect(() => {
		const query = page.url.searchParams.get('q') ?? '';
		untrack(() => model.setQuery(query));
	});
	onMount(() => {
		void model.connect();
		return () => model.dispose();
	});
</script>

<svelte:head><title>{t('historyTitle')}</title></svelte:head>

<InternalPage title={t('historyTitle')} titleId="history-title">
	{#snippet icon()}<History size={19} strokeWidth={1.6} aria-hidden="true" />{/snippet}
	{#snippet actions()}
		<Button href="chrome://history/syncedTabs" variant="ghost">{t('historyOtherDevices')}</Button>
		<Button
			variant="outline"
			disabled={model.loading || !!model.error}
			onclick={() => model.clearBrowsingData()}>{t('historyClearData')}</Button
		>
	{/snippet}
	{#snippet toolbar()}
		<SearchField
			value={model.query}
			label={t('historySearch')}
			clearLabel={t('historyClearSearch')}
			oninput={setQuery}
		/>
		{#if model.allowDeletingHistory && model.items.length}<div class="selection">
				<Checkbox
					id="history-select-all"
					checked={model.allSelected}
					indeterminate={model.selected.size > 0 && !model.allSelected}
					disabled={model.loading || model.deleting || !!model.error}
					onCheckedChange={(checked) => model.selectAll(checked)}
					aria-label={t('historySelectAll')}
				/>
				<Label for="history-select-all" class="text-xs font-normal text-muted-foreground"
					>{t('historySelectAll')}</Label
				>
				{#if model.selected.size}<Button
						variant="ghost"
						disabled={model.loading || model.deleting || !!model.error}
						onclick={() => model.requestRemoval()}
						>{t('historyRemoveSelected', String(model.selected.size))}</Button
					>{/if}
			</div>{/if}
	{/snippet}
	<div class="feedback" role="status" aria-live="polite">{model.notice}</div>
	{#if model.error}<div class="empty" role="alert">
			<p>{model.error}</p>
			<Button variant="outline" onclick={() => location.reload()}>{t('historyReload')}</Button>
		</div>{:else}
		<section aria-label={t('historyTitle')} aria-busy={model.loading}>
			<ul data-history-list>
				{#each model.items as item, index (entryKey(item))}
					{#if index === 0 || item.dateShort !== model.items[index - 1]?.dateShort}<li class="date">
							<h2>{item.dateRelativeDay || item.dateShort}</h2>
						</li>{/if}
					<HistoryRow
						{item}
						selected={model.selected.has(entryKey(item))}
						allowDeletingHistory={model.allowDeletingHistory}
						disabled={model.loading || model.deleting || !!model.error}
						onselect={(checked) => model.toggle(item, checked)}
						onremove={() => model.requestRemoval([item])}
						onopen={(event) => model.open(item, event)}
					/>
				{/each}
			</ul>
			{#if !model.items.length}<div class="empty">
					<History size={28} strokeWidth={1.2} aria-hidden="true" />
					<p>
						{model.loading
							? t('historyLoading')
							: model.query.trim()
								? t('historyNoResults')
								: t('historyEmpty')}
					</p>
				</div>{:else if model.more}<div class="load-more">
					<Button variant="ghost" disabled={model.loading} onclick={() => model.loadMore()}>
						{model.loading ? t('historyLoading') : t('historyLoadMore')}
					</Button>
				</div>{/if}
		</section>
	{/if}
	{#if model.otherForms}<footer>
			<p>{t('historyOtherActivity')}</p>
			<Button href={model.activityUrl} variant="link" target="_blank" rel="noopener noreferrer"
				>{t('historyManageActivity')}</Button
			>
		</footer>{/if}
</InternalPage>
<HistoryDeletionDialog {model} />

<style>
	@layer components {
		.selection {
			display: flex;
			align-items: center;
			gap: 9px;
			color: var(--muted-foreground);
			font-size: 12px;
		}
		.feedback {
			color: var(--muted-foreground);
		}
		.feedback:not(:empty) {
			padding: 12px 0;
		}
		ul {
			padding: 0;
			margin: 0;
			list-style: none;
		}
		.date {
			padding: 20px 8px 3px;
		}
		h2 {
			font-size: 12px;
			font-weight: 500;
			color: var(--muted-foreground);
			margin: 0;
		}
		.empty {
			display: flex;
			flex-direction: column;
			align-items: center;
			justify-content: center;
			gap: 12px;
			min-height: 240px;
			color: var(--muted-foreground);
			text-align: center;
		}
		.empty p {
			margin: 0;
		}
		.load-more {
			display: flex;
			justify-content: center;
			padding-top: 22px;
		}
		footer {
			margin-top: 30px;
			color: var(--muted-foreground);
			font-size: 12px;
		}
		footer p {
			margin: 0;
		}
	}
</style>
