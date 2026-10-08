<script lang="ts">
	import { onMount } from 'svelte';
	import { mergeProps } from 'bits-ui';
	import { page } from '$app/state';
	import { goto } from '$app/navigation';
	import Download from '@lucide/svelte/icons/download';
	import FolderOpen from '@lucide/svelte/icons/folder-open';
	import Search from '@lucide/svelte/icons/search';
	import X from '@lucide/svelte/icons/x';
	import { Button } from '#lib/components/ui/button/index.ts';
	import { Input } from '#lib/components/ui/input/index.ts';
	import Hint from '#lib/components/Hint.svelte';
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

<main aria-labelledby="downloads-title">
	<div class="page">
		<header>
			<div class="heading">
				<Download size={19} strokeWidth={1.6} aria-hidden="true" />
				<h1 id="downloads-title">{t('downloadsTitle')}</h1>
			</div>
			<div class="header-actions">
				<Button
					variant="outline"
					disabled={!model.states || !!model.error}
					onclick={() => model.openFolder()}
					><FolderOpen size={15} />{t('downloadsOpenFolder')}</Button
				>
				{#if model.allowDeletingHistory}<Button
						variant="ghost"
						disabled={model.loading || !!model.error || !model.items.length || !!model.query}
						onclick={() => model.clear()}>{t('downloadsClear')}</Button
					>{/if}
			</div>
		</header>
		<div class="toolbar" role="search">
			<div class="search-field">
				<Search size={15} aria-hidden="true" /><Input
					type="search"
					value={model.query}
					oninput={(event) => setQuery(event.currentTarget.value)}
					placeholder={t('downloadsSearch')}
					aria-label={t('downloadsSearch')}
					class="border-0 bg-transparent pl-8 shadow-none"
				/>
				{#if model.query}<Hint text={t('downloadsClearSearch')}>
						{#snippet children({ props })}
							<Button
								{...mergeProps(props, { onclick: () => setQuery('') })}
								size="icon-sm"
								variant="ghost"
								class="clear-search"
								aria-label={t('downloadsClearSearch')}><X size={14} /></Button
							>
						{/snippet}
					</Hint>{/if}
			</div>
		</div>
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
	</div>
</main>

<style>
	@layer components {
		main {
			height: 100dvh;
			overflow-y: auto;
			overscroll-behavior: contain;
		}
		.page {
			max-width: 960px;
			margin: 0 auto;
			padding: 38px 36px 64px;
		}
		header {
			display: flex;
			justify-content: space-between;
			align-items: center;
			gap: 20px;
			margin-bottom: 25px;
		}
		.heading,
		.header-actions {
			display: flex;
			align-items: center;
			gap: 10px;
		}
		.heading :global(svg) {
			color: var(--muted-foreground);
		}
		h1 {
			font-size: 18px;
			font-weight: 550;
			letter-spacing: -0.4px;
			margin: 0;
		}
		.toolbar {
			display: flex;
			align-items: center;
			justify-content: space-between;
			gap: 12px;
			padding-bottom: 14px;
			border-bottom: 1px solid var(--border);
		}
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
		@media (max-width: 620px) {
			.page {
				padding: 26px 20px 48px;
			}
			header {
				align-items: flex-start;
				flex-wrap: wrap;
				gap: 14px;
			}
			.toolbar {
				flex-wrap: wrap;
			}
			.search-field {
				width: 100%;
			}
		}
	}
</style>
