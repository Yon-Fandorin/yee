<script lang="ts">
	import { onMount } from 'svelte';
	import { page } from '$app/state';
	import { goto } from '$app/navigation';
	import Download from '@lucide/svelte/icons/download';
	import FolderOpen from '@lucide/svelte/icons/folder-open';
	import { Button } from '#lib/components/ui/button/index.ts';
	import Hint from '#lib/components/Hint.svelte';
	import InternalPage from '#lib/components/InternalPage.svelte';
	import SearchField from '#lib/components/SearchField.svelte';
	import { t } from '#lib/i18n.ts';
	import DownloadRow from './components/DownloadRow.svelte';
	import { DownloadsModel } from './downloads.svelte.ts';

	const model = new DownloadsModel(page.url.searchParams.get('q') ?? '');
	function setQuery(value: string) {
		model.setQuery(value);
		const url = new URL(page.url.href);
		if (value) url.searchParams.set('q', value);
		else url.searchParams.delete('q');
		void goto(url, { shallow: true, replace: true, state: page.state });
	}
	$effect(() => model.setQuery(page.url.searchParams.get('q') ?? ''));
	onMount(() => {
		void model.connect();
		return () => model.dispose();
	});
</script>

<svelte:head><title>{t('downloadsTitle')}</title></svelte:head>

<InternalPage title={t('downloadsTitle')} titleId="downloads-title">
	{#snippet icon()}<Download size={19} strokeWidth={1.6} aria-hidden="true" />{/snippet}
	{#snippet actions()}
		<Button
			variant="outline"
			disabled={!model.states || !!model.error}
			onclick={() => model.openFolder()}
		>
			<FolderOpen size={15} />{t('downloadsOpenFolder')}
		</Button>
		{#if model.allowDeletingHistory}<Button
				variant="ghost"
				disabled={model.loading || !!model.error || !model.items.length || !!model.query}
				onclick={() => model.clear()}>{t('downloadsClear')}</Button
			>{/if}
	{/snippet}
	{#snippet toolbar()}
		<SearchField
			value={model.query}
			label={t('downloadsSearch')}
			clearLabel={t('downloadsClearSearch')}
			oninput={setQuery}
		/>
	{/snippet}
	<div class="feedback" role="status" aria-live="polite">
		{#if model.removed}<span>{t('downloadsRemoved')}</span>
			{#if model.undoable}<Button
					variant="link"
					disabled={!!model.error}
					onclick={() => model.undo()}>{t('downloadsUndo')}</Button
				>{/if}
		{/if}
	</div>
	{#if model.error}
		<div class="empty" role="alert">
			<p>{model.error}</p>
			<Button variant="outline" onclick={() => location.reload()}>{t('downloadsReload')}</Button>
		</div>
	{:else if model.states}
		<section aria-label={t('downloadsTitle')} aria-busy={model.loading}>
			<ul class="downloads" data-download-list>
				{#each model.items as item, index (item.id)}
					{#if index === 0 || item.dateString !== model.items[index - 1]?.dateString}<li
							class="date"
						>
							<Hint text={item.dateString}>
								{#snippet children({ props })}<h2 {...props}>
										{item.sinceString || item.dateString}
									</h2>{/snippet}
							</Hint>
						</li>{/if}
					<DownloadRow
						{item}
						states={model.states}
						allowDeletingHistory={model.allowDeletingHistory}
						disabled={!!model.error}
						onaction={(action, data) => model.act(action, data)}
					/>
				{/each}
			</ul>
			{#if !model.items.length}<div class="empty">
					<Download size={28} strokeWidth={1.2} aria-hidden="true" />
					<p>
						{model.loading
							? t('downloadsLoading')
							: model.query.trim()
								? t('downloadsNoResults')
								: t('downloadsEmpty')}
					</p>
				</div>
			{:else if model.more}<div class="load-more">
					<Button variant="ghost" disabled={model.loading} onclick={() => model.loadMore()}
						>{model.loading ? t('downloadsLoading') : t('downloadsLoadMore')}</Button
					>
				</div>{/if}
		</section>
	{:else}<div class="empty" role="status">{t('downloadsLoading')}</div>{/if}
</InternalPage>

<style>
	@layer components {
		.feedback {
			display: flex;
			align-items: center;
			gap: 10px;
			color: var(--muted-foreground);
		}
		.feedback:not(:empty) {
			padding: 12px 0;
		}
		.downloads {
			padding: 0;
			margin: 0;
			list-style: none;
		}
		.date {
			padding: 20px 8px 3px;
		}
		h2 {
			width: fit-content;
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
	}
</style>
