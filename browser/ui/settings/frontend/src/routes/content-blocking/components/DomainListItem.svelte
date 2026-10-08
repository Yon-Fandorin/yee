<script lang="ts">
	import { tick } from 'svelte';
	import type { BlockedDomain } from '#lib/bridge.ts';
	import { Button } from '#lib/components/ui/button/index.ts';
	import { Checkbox } from '#lib/components/ui/checkbox/index.ts';
	import { Label } from '#lib/components/ui/label/index.ts';
	import { t } from '#lib/i18n.ts';
	import { useSettings } from '#lib/settings.svelte.ts';
	let {
		rule,
		selected,
		disabled,
		onselect,
		onremove
	}: {
		rule: BlockedDomain;
		selected: boolean;
		disabled: boolean;
		onselect: (checked: boolean) => void;
		onremove: () => void;
	} = $props();
	const model = useSettings();
	const scopeId = $props.id();
	let scope = $state(false);
	let scopeControl = $state<HTMLButtonElement | null>(null);
	$effect(() => {
		scope = rule.includeSubdomains;
	});
	async function changeScope(value: boolean) {
		await model.changeDomainScope(rule.domain, value);
		scope = rule.includeSubdomains;
		await tick();
		scopeControl?.focus();
	}
</script>

<li>
	<Checkbox
		checked={selected}
		{disabled}
		aria-label={t('selectDomainLabel', rule.domain)}
		onCheckedChange={onselect}
	/>
	<div class="domain-detail">
		<span class="domain-host">{rule.domain}</span>
		<div class="scope-control">
			<Checkbox
				id={scopeId}
				bind:ref={scopeControl}
				bind:checked={scope}
				{disabled}
				aria-label={t('domainScopeLabel', rule.domain)}
				onCheckedChange={changeScope}
			/>
			<Label for={scopeId} class="font-normal text-muted-foreground">{t('includeSubdomains')}</Label
			>
		</div>
	</div>
	<Button
		variant="ghost"
		class="text-muted-foreground"
		{disabled}
		aria-label={t('removeDomainLabel', rule.domain)}
		onclick={onremove}>{t('removeDomain')}</Button
	>
</li>

<style>
	@layer components {
		li {
			display: flex;
			align-items: center;
			gap: 12px;
			min-height: 60px;
			padding: 12px 16px;
		}
		li:not(:first-child) {
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
		.scope-control {
			display: flex;
			align-items: center;
			gap: 7px;
			margin-top: 5px;
		}
	}
</style>
