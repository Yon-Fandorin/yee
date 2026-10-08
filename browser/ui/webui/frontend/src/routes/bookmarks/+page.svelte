<script lang="ts">
	import { onMount, untrack } from 'svelte';
	import { page } from '$app/state';
	import { goto } from '$app/navigation';
	import Bookmark from '@lucide/svelte/icons/bookmark';
	import Folder from '@lucide/svelte/icons/folder';
	import Plus from '@lucide/svelte/icons/plus';
	import Ellipsis from '@lucide/svelte/icons/ellipsis';
	import ChevronRight from '@lucide/svelte/icons/chevron-right';
	import { Button } from '#lib/components/ui/button/index.ts';
	import * as DropdownMenu from '#lib/components/ui/dropdown-menu/index.ts';
	import InternalPage from '#lib/components/InternalPage.svelte';
	import SearchField from '#lib/components/SearchField.svelte';
	import Hint from '#lib/components/Hint.svelte';
	import { t } from '#lib/i18n.ts';
	import { BookmarksModel } from './bookmarks.svelte.ts';
	import BookmarkFolders from './components/BookmarkFolders.svelte';
	import BookmarkRow from './components/BookmarkRow.svelte';
	import BookmarkEditor from './components/BookmarkEditor.svelte';
	import BookmarkDeletionDialog from './components/BookmarkDeletionDialog.svelte';
	let model = $state<BookmarksModel | null>(null);
	function navigate(id: string, replace = false) {
		const url = new URL(page.url.href);
		url.searchParams.set('id', id);
		if (!replace) url.searchParams.delete('q');
		url.hash = '';
		void goto(url, { shallow: true, replace, state: page.state });
	}
	function setQuery(value: string) {
		model?.setQuery(value);
		const url = new URL(page.url.href);
		if (value) url.searchParams.set('q', value);
		else url.searchParams.delete('q');
		void goto(url, { shallow: true, replace: true, state: page.state });
	}
	$effect(() => {
		const state = model;
		const id = page.url.searchParams.get('id') ?? '';
		const query = page.url.searchParams.get('q') ?? '';
		untrack(() => state?.syncLocation(id, query));
	});
	onMount(() => {
		const state = new BookmarksModel(navigate);
		model = state;
		void state.connect();
		return () => state.dispose();
	});
</script>

<svelte:head><title>{t('bookmarksTitle')}</title></svelte:head>

<InternalPage title={t('bookmarksTitle')} titleId="bookmarks-title">
	{#snippet icon()}<Bookmark size={20} strokeWidth={1.5} aria-hidden="true" />{/snippet}
	{#snippet actions()}
		{#if model}
			{#if model.canAdd}
				<DropdownMenu.Root>
					<DropdownMenu.Trigger>
						{#snippet child({ props })}<Button
								{...props}
								variant="outline"
								disabled={model?.busy ?? true}><Plus size={14} />{t('bookmarksAdd')}</Button
							>{/snippet}
					</DropdownMenu.Trigger>
					<DropdownMenu.Content align="end">
						<DropdownMenu.Item
							onclick={() => model?.openEditor({ kind: 'bookmark', parentId: model.folderId })}
							>{t('bookmarksAddBookmark')}</DropdownMenu.Item
						>
						<DropdownMenu.Item
							onclick={() => model?.openEditor({ kind: 'folder', parentId: model.folderId })}
							>{t('bookmarksAddFolder')}</DropdownMenu.Item
						>
					</DropdownMenu.Content>
				</DropdownMenu.Root>
			{/if}
			<DropdownMenu.Root>
				<Hint text={t('bookmarksMore')}>
					{#snippet children({ props: tooltipProps })}
						<DropdownMenu.Trigger {...tooltipProps}>
							{#snippet child({ props })}
								<Button
									{...props}
									variant="ghost"
									size="icon"
									aria-label={t('bookmarksMore')}
									disabled={model?.busy ?? true}><Ellipsis size={16} /></Button
								>
							{/snippet}
						</DropdownMenu.Trigger>
					{/snippet}
				</Hint>
				<DropdownMenu.Content align="end">
					<DropdownMenu.Item disabled={!model.canEdit} onclick={() => model?.transfer('import')}
						>{t('bookmarksImport')}</DropdownMenu.Item
					>
					<DropdownMenu.Item onclick={() => model?.transfer('export')}
						>{t('bookmarksExport')}</DropdownMenu.Item
					>
				</DropdownMenu.Content>
			</DropdownMenu.Root>
		{/if}
	{/snippet}
	{#snippet toolbar()}
		<SearchField
			value={model?.query ?? ''}
			label={t('bookmarksSearch')}
			clearLabel={t('bookmarksClearSearch')}
			oninput={setQuery}
		/>
		{#if model?.selected.length}
			<div class="selection-actions">
				<span>{t('bookmarksSelected', String(model.selected.length))}</span>
				<Button
					variant="ghost"
					disabled={model.busy}
					onclick={() => model?.requestRemoval(model.selected)}>{t('bookmarksDelete')}</Button
				>
				<Button
					variant="ghost"
					disabled={model.busy}
					onclick={() => {
						if (model) model.selected = [];
					}}>{t('bookmarksClearSelection')}</Button
				>
			</div>
		{/if}
	{/snippet}
	{#if model}
		<div class="feedback" aria-live="polite">
			{#if model.error}<span role="alert">{model.error}</span>
				{#if model.loadFailed}<Button variant="ghost" onclick={() => model?.refresh()}
						>{t('bookmarksRetry')}</Button
					>{/if}
			{:else if model.notice}<span>{model.notice}</span>
				{#if model.undoable}<Button
						variant="ghost"
						disabled={model.busy || !model.canEdit}
						onclick={() => model?.undo()}>{t('bookmarksUndo')}</Button
					>{/if}
			{/if}
		</div>
		{#if !model.canEdit && !model.loading && !model.loadFailed}<p class="policy">
				{t('bookmarksReadOnly')}
			</p>{/if}
		<div class="workspace">
			<BookmarkFolders {model} />
			<section aria-label={t('bookmarksList')} aria-busy={model.loading}>
				{#if model.query.trim()}<h2>{t('bookmarksSearchResults')}</h2>
				{:else}
					<nav class="breadcrumb" aria-label={t('bookmarksLocation')}>
						{#each model.breadcrumbs as node, index (node.id)}
							{#if index}<ChevronRight size={12} aria-hidden="true" />{/if}
							<Button
								variant="ghost"
								aria-current={node.id === model.folderId ? 'page' : undefined}
								onclick={() => model?.navigate(node.id)}
								>{node.title || t('bookmarksUntitled')}</Button
							>
						{/each}
					</nav>
				{/if}
				<ul class="bookmarks" data-bookmark-list>
					{#each model.items as node (node.id)}<BookmarkRow {node} {model} />{/each}
				</ul>
				{#if !model.items.length}
					<div class="empty" role="status">
						<Folder size={28} strokeWidth={1.2} aria-hidden="true" />
						<p>
							{t(
								model.loading
									? 'bookmarksLoading'
									: model.loadFailed
										? 'bookmarksUnavailable'
										: model.query.trim()
											? 'bookmarksNoResults'
											: 'bookmarksEmpty'
							)}
						</p>
					</div>
				{/if}
			</section>
		</div>
		<BookmarkEditor {model} />
		<BookmarkDeletionDialog {model} />
	{:else}<div class="empty" role="status">{t('bookmarksLoading')}</div>{/if}
</InternalPage>

<style>
	@layer components {
		.feedback {
			display: flex;
			align-items: center;
			gap: 10px;
			color: var(--muted-foreground);
			font-size: 12px;
		}
		.feedback:not(:empty) {
			padding: 12px 0;
		}
		.policy {
			margin: 14px 0 0;
			color: var(--muted-foreground);
			font-size: 12px;
		}
		.workspace {
			display: grid;
			grid-template-columns: 180px minmax(0, 1fr);
			gap: 28px;
			padding-top: 18px;
		}
		section {
			min-width: 0;
		}
		.bookmarks {
			list-style: none;
			margin: 0;
			padding: 0;
		}
		h2 {
			font-size: 12px;
			font-weight: 500;
			color: var(--muted-foreground);
			margin: 0 0 12px;
			padding: 5px 8px;
		}
		.breadcrumb {
			display: flex;
			align-items: center;
			flex-wrap: wrap;
			color: var(--muted-foreground);
			gap: 0;
			min-height: 28px;
			margin-bottom: 8px;
		}
		.breadcrumb :global([aria-current='page']) {
			color: var(--foreground);
		}
		.selection-actions {
			display: flex;
			align-items: center;
			flex-wrap: wrap;
			gap: 4px;
			font-size: 11px;
			color: var(--muted-foreground);
		}
		.selection-actions span {
			margin-inline-end: 6px;
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
		@media (max-width: 700px) {
			.workspace {
				grid-template-columns: minmax(0, 1fr);
				gap: 16px;
			}
		}
	}
</style>
