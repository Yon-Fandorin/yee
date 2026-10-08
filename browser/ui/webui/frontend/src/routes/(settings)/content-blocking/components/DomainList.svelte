<script lang="ts">
	import { tick, untrack } from 'svelte';
	import DomainListItem from './DomainListItem.svelte';
	import { Checkbox } from '#lib/components/ui/checkbox/index.ts';
	import { Button } from '#lib/components/ui/button/index.ts';
	import { Input } from '#lib/components/ui/input/index.ts';
	import { Label } from '#lib/components/ui/label/index.ts';
	import { t } from '#lib/i18n.ts';
	import { useSettings } from '#settings/settings.svelte.ts';
	let { onremoved }: { onremoved: () => void } = $props();
	const model = useSettings();
	let query = $state('');
	let searchInput = $state<HTMLInputElement | null>(null);
	let selected = $state(new Set<string>());
	let page = $state(0);
	const pageSize = 50;
	const domains = $derived(model.state?.blockedDomains ?? []);
	const filtered = $derived(
		domains.filter((rule) => rule.domain.includes(query.trim().toLowerCase()))
	);
	const pageCount = $derived(Math.max(1, Math.ceil(filtered.length / pageSize)));
	const currentPage = $derived(Math.min(page, pageCount - 1));
	const visible = $derived(filtered.slice(currentPage * pageSize, (currentPage + 1) * pageSize));
	const pageSelected = $derived(
		visible.length > 0 && visible.every((rule) => selected.has(rule.domain))
	);
	const pagePartiallySelected = $derived(
		!pageSelected && visible.some((rule) => selected.has(rule.domain))
	);
	const disabled = $derived(model.changingDomains || !model.state?.enabled);

	$effect(() => {
		const registered = new Set(domains.map((rule) => rule.domain));
		selected = new Set(untrack(() => [...selected]).filter((domain) => registered.has(domain)));
	});
	function select(domain: string, checked: boolean) {
		const next = new Set(selected);
		if (checked) next.add(domain);
		else next.delete(domain);
		selected = next;
	}
	function selectPage(checked: boolean) {
		const next = new Set(selected);
		for (const rule of visible) {
			if (checked) next.add(rule.domain);
			else next.delete(rule.domain);
		}
		selected = next;
	}
	async function remove(domains: string[]) {
		const removed =
			domains.length === 1
				? await model.removeDomain(domains[0])
				: await model.removeDomains(domains);
		if (removed) {
			await tick();
			if (searchInput?.isConnected) searchInput.focus();
			else onremoved();
		}
	}
</script>

{#if domains.length}
	<div class="management">
		<Label for="domain-search">{t('searchDomains')}</Label>
		<Input
			id="domain-search"
			bind:ref={searchInput}
			bind:value={query}
			class="mt-2 h-8 bg-background text-xs md:text-xs"
			autocomplete="off"
			spellcheck="false"
			{disabled}
			oninput={() => {
				page = 0;
				selected = new Set();
			}}
		/>
		<p class="count" role="status">
			{t('domainCount', String(filtered.length), String(domains.length))}
		</p>
		<div class="selection">
			<div class="page-select">
				<Checkbox
					id="select-domain-page"
					checked={pageSelected}
					indeterminate={pagePartiallySelected}
					disabled={disabled || !visible.length}
					onCheckedChange={selectPage}
				/>
				<Label for="select-domain-page" class="font-normal text-muted-foreground"
					>{t('selectDomainPage')}</Label
				>
			</div>
			<Button
				id="remove-selected-domains"
				variant="outline"
				disabled={disabled || !selected.size}
				onclick={() => remove([...selected])}
				>{t('removeSelectedDomains', String(selected.size))}</Button
			>
		</div>
	</div>
	{#if filtered.length}
		<ul id="blocked-domains" class="domain-list" aria-label={t('domainsTitle')}>
			{#each visible as rule (rule.domain)}
				<DomainListItem
					{rule}
					{disabled}
					selected={selected.has(rule.domain)}
					onselect={(checked) => select(rule.domain, checked)}
					onremove={() => remove([rule.domain])}
				/>
			{/each}
		</ul>
	{:else}<p class="empty">{t('domainsNoMatches')}</p>{/if}
	{#if pageCount > 1}<div class="pagination">
			<Button
				variant="ghost"
				disabled={currentPage === 0 || disabled}
				onclick={() => (page = currentPage - 1)}>{t('previousPage')}</Button
			>
			<span>{currentPage + 1} / {pageCount}</span>
			<Button
				variant="ghost"
				disabled={currentPage === pageCount - 1 || disabled}
				onclick={() => (page = currentPage + 1)}>{t('nextPage')}</Button
			>
		</div>{/if}
{:else if model.state}<p class="empty">{t('domainsEmpty')}</p>{/if}

<style>
	@layer components {
		.management {
			padding: 16px;
			border-top: 1px solid var(--border);
		}
		.count {
			margin: 8px 0;
			font-size: 12px;
			color: var(--muted-foreground);
		}
		.selection {
			display: flex;
			align-items: center;
			justify-content: space-between;
			gap: 12px;
		}
		.page-select {
			display: flex;
			align-items: center;
			gap: 7px;
			font-size: 12px;
			color: var(--muted-foreground);
		}
		.domain-list {
			list-style: none;
			padding: 0;
			margin: 0;
			border-top: 1px solid var(--border);
		}
		.empty {
			margin: 0;
			padding: 23px 20px;
			border-top: 1px solid var(--border);
			text-align: center;
			font-size: 12px;
			color: var(--muted-foreground);
		}
		.pagination {
			display: flex;
			justify-content: space-between;
			align-items: center;
			padding: 8px 12px;
			border-top: 1px solid var(--border);
			font-size: 12px;
			color: var(--muted-foreground);
		}
	}
</style>
