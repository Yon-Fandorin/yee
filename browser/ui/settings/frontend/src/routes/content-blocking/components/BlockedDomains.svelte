<script lang="ts">
	import { tick } from 'svelte';
	import type { BlockedDomain } from '#lib/bridge.ts';
	import SettingsGroup from '#lib/components/SettingsGroup.svelte';
	import SettingsNotice from '#lib/components/SettingsNotice.svelte';
	import { Button } from '#lib/components/ui/button/index.ts';
	import { Input } from '#lib/components/ui/input/index.ts';
	import { Label } from '#lib/components/ui/label/index.ts';
	import { t } from '#lib/i18n.ts';
	import { useSettings } from '#lib/settings.svelte.ts';
	import DomainImport from './DomainImport.svelte';
	const model = useSettings();
	let input = $state('');
	let includeSubdomains = $state(true);
	let domainInput = $state<HTMLInputElement | null>(null);
	let page = $state(0);
	const pageSize = 50;
	const domains = $derived(model.state?.blockedDomains ?? []);
	const pageCount = $derived(Math.max(1, Math.ceil(domains.length / pageSize)));
	const currentPage = $derived(Math.min(page, pageCount - 1));
	const visibleDomains = $derived(
		domains.slice(currentPage * pageSize, (currentPage + 1) * pageSize)
	);
	const disabled = $derived(model.changingDomains || !model.state?.enabled);

	async function add(event: SubmitEvent) {
		event.preventDefault();
		if (await model.addDomain(input, includeSubdomains)) input = '';
		await tick();
		domainInput?.focus();
	}

	async function remove(domain: string) {
		if (await model.removeDomain(domain)) {
			await tick();
			domainInput?.focus();
		}
	}

	async function changeScope(rule: BlockedDomain, event: Event) {
		const checkbox = event.currentTarget as HTMLInputElement;
		const checked = checkbox.checked;
		checkbox.checked = rule.includeSubdomains;
		await model.changeDomainScope(rule.domain, checked);
		await tick();
		if (checkbox.isConnected) checkbox.focus();
		else domainInput?.focus();
	}
</script>

<SettingsGroup title={t('domainsTitle')} description={t('domainsDescription')}>
	<form id="domain-form" onsubmit={add}>
		<Label for="domain-input">{t('domainAddress')}</Label>
		<div class="input-row">
			<Input
				id="domain-input"
				bind:ref={domainInput}
				bind:value={input}
				class="h-8 bg-background text-xs md:text-xs"
				placeholder={t('sitePlaceholder')}
				required
				maxlength={1024}
				autocomplete="off"
				spellcheck="false"
				{disabled}
			/>
			<Button id="add-domain" type="submit" size="lg" {disabled}>{t('addDomain')}</Button>
		</div>
		<label class="scope-control"
			><input
				id="include-subdomains"
				type="checkbox"
				bind:checked={includeSubdomains}
				{disabled}
			/>{t('includeSubdomains')}</label
		>
	</form>
	<div
		class="domain-feedback"
		class:has-message={Boolean(model.domainFeedback)}
		role={model.domainFeedbackError ? 'alert' : 'status'}
		aria-live={model.domainFeedbackError ? 'assertive' : 'polite'}
		aria-atomic="true"
	>
		{#if model.domainFeedback}
			<SettingsNotice
				id="domain-feedback"
				message={model.domainFeedback}
				error={model.domainFeedbackError}
			/>
		{/if}
	</div>
	<DomainImport />
	{#if domains.length}
		<ul id="blocked-domains" class="domain-list" aria-label={t('domainsTitle')}>
			{#each visibleDomains as rule (rule.domain)}
				<li>
					<div class="domain-detail">
						<span class="domain-host">{rule.domain}</span>
						<label class="scope-control"
							><input
								type="checkbox"
								checked={rule.includeSubdomains}
								{disabled}
								aria-label={t('domainScopeLabel', rule.domain)}
								onchange={(event) => changeScope(rule, event)}
							/>{t('includeSubdomains')}</label
						>
					</div>
					<Button
						variant="ghost"
						class="text-muted-foreground"
						{disabled}
						aria-label={t('removeDomainLabel', rule.domain)}
						onclick={() => remove(rule.domain)}>{t('removeDomain')}</Button
					>
				</li>
			{/each}
		</ul>
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
</SettingsGroup>

<style>
	@layer components {
		#domain-form {
			padding: 16px;
		}
		.domain-feedback.has-message {
			padding: 0 16px 16px;
		}
		.input-row {
			display: flex;
			align-items: center;
			gap: 8px;
			margin-top: 9px;
		}
		.scope-control {
			display: flex;
			align-items: center;
			gap: 7px;
			margin-top: 9px;
			font-size: 12px;
			color: var(--muted-foreground);
		}
		input[type='checkbox'] {
			accent-color: var(--primary);
		}
		.domain-list {
			list-style: none;
			padding: 0;
			margin: 0;
			border-top: 1px solid var(--border);
		}
		.domain-list li {
			display: flex;
			align-items: center;
			gap: 10px;
			min-height: 60px;
			padding: 12px 16px;
		}
		.domain-list li + li {
			border-top: 1px solid var(--border);
		}
		.domain-detail {
			min-width: 0;
			flex: 1;
		}
		.domain-host {
			overflow-wrap: anywhere;
			font-size: 12px;
		}
		.domain-detail .scope-control {
			margin-top: 5px;
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
