<script lang="ts">
	import { onMount, untrack } from 'svelte';
	import { page } from '$app/state';
	import { goto } from '$app/navigation';
	import Puzzle from '@lucide/svelte/icons/puzzle';
	import ExternalLink from '@lucide/svelte/icons/external-link';
	import { Button } from '#lib/components/ui/button/index.ts';
	import InternalPage from '#lib/components/InternalPage.svelte';
	import SearchField from '#lib/components/SearchField.svelte';
	import { t } from '#lib/i18n.ts';
	import { currentPageURL } from '#lib/navigation.ts';
	import { ExtensionsModel } from './extensions.svelte.ts';
	import ExtensionRow from './components/ExtensionRow.svelte';
	import ExtensionDetails from './components/ExtensionDetails.svelte';
	const model = new ExtensionsModel();
	const currentURL = $derived(currentPageURL());
	const selectedId = $derived(currentURL.searchParams.get('id'));
	const selected = $derived(model.items.find((item) => item.id === selectedId));
	function routeURL(id?: string) {
		const url = new URL(currentPageURL().href);
		if (id) url.searchParams.set('id', id);
		else url.searchParams.delete('id');
		return url;
	}
	function navigate(id?: string) {
		void goto(routeURL(id), { shallow: true, state: page.state });
	}
	function setQuery(value: string) {
		model.setQuery(value);
		const url = new URL(currentPageURL().href);
		if (value) url.searchParams.set('q', value);
		else url.searchParams.delete('q');
		void goto(url, { shallow: true, replace: true, state: page.state });
	}
	$effect(() => {
		const query = currentURL.searchParams.get('q') ?? '';
		untrack(() => model.setQuery(query));
	});
	onMount(() => {
		void model.connect();
		return () => model.dispose();
	});
</script>

<svelte:head><title>{t('extensionsTitle')}</title></svelte:head>

{#snippet listToolbar()}
	<div class="search" data-extension-search>
		<SearchField
			value={model.query}
			label={t('extensionsSearch')}
			clearLabel={t('extensionsClearSearch')}
			oninput={setQuery}
		/>
	</div>
	<span class="count">{t('extensionsCount', String(model.items.length))}</span>
{/snippet}

<InternalPage
	title={t('extensionsTitle')}
	titleId="extensions-title"
	toolbar={!selectedId ? listToolbar : undefined}
>
	{#snippet icon()}<Puzzle size={19} strokeWidth={1.6} aria-hidden="true" />{/snippet}
	{#snippet actions()}
		<Button href={model.nativeURL(undefined, 'shortcuts')} variant="ghost"
			>{t('extensionsShortcuts')}<ExternalLink size={12} strokeWidth={1.5} /></Button
		>
		<Button href={model.nativeURL()} variant="outline"
			>{t('extensionsDeveloperTools')}<ExternalLink size={12} strokeWidth={1.5} /></Button
		>
	{/snippet}
	<div class="notice" data-extension-notice role="status" aria-live="polite">
		{!model.loadFailed ? model.error : ''}
	</div>
	{#if model.loadFailed}
		<div class="empty" role="alert">
			<p>{model.error}</p>
			<div class="empty-actions">
				<Button variant="outline" onclick={() => model.connect()}>{t('extensionsRetry')}</Button
				><Button href={model.nativeURL()} variant="ghost">{t('extensionsOpenNative')}</Button>
			</div>
		</div>
	{:else if selectedId}
		{#if selected}{#key selected.id}<ExtensionDetails
					item={selected}
					{model}
					backURL={routeURL().href}
					onback={() => navigate()}
				/>{/key}{:else}<div class="empty">
				<p>{model.loading ? t('extensionsLoading') : t('extensionsNotFound')}</p>
				<Button
					href={routeURL().href}
					variant="ghost"
					onclick={(event) => {
						if (event.button || event.metaKey || event.ctrlKey || event.shiftKey || event.altKey)
							return;
						event.preventDefault();
						navigate();
					}}>{t('extensionsBack')}</Button
				>
			</div>{/if}
	{:else}
		<section aria-label={t('extensionsTitle')} aria-busy={model.loading}>
			<ul data-extension-list>
				{#each model.visibleItems as item (item.id)}<ExtensionRow
						{item}
						{model}
						detailsURL={routeURL(item.id).href}
						ondetails={() => navigate(item.id)}
					/>{/each}
			</ul>
			{#if !model.visibleItems.length}<div class="empty">
					<Puzzle size={28} strokeWidth={1.2} aria-hidden="true" />
					<p>
						{model.loading
							? t('extensionsLoading')
							: model.query.trim()
								? t('extensionsNoResults')
								: t('extensionsEmpty')}
					</p>
					{#if !model.loading && !model.query.trim()}<Button
							href="https://chromewebstore.google.com/category/extensions"
							variant="outline"
							>{t('extensionsFindExtensions')}<ExternalLink size={12} strokeWidth={1.5} /></Button
						>{/if}
				</div>{/if}
		</section>
	{/if}
</InternalPage>

<style>
	@layer components {
		.search {
			width: min(100%, 340px);
		}
		.count {
			color: var(--muted-foreground);
			font-size: 11px;
		}
		.notice {
			color: var(--destructive);
			font-size: 12px;
		}
		.notice:not(:empty) {
			padding: 14px 0;
		}
		ul {
			list-style: none;
			padding: 0;
			margin: 0;
		}
		.empty {
			display: flex;
			flex-direction: column;
			justify-content: center;
			align-items: center;
			gap: 12px;
			min-height: 260px;
			color: var(--muted-foreground);
			text-align: center;
		}
		.empty p {
			margin: 0;
			font-size: 13px;
		}
		.empty-actions {
			display: flex;
			gap: 8px;
		}
		@media (max-width: 620px) {
			.search {
				width: 100%;
			}
		}
	}
</style>
