<script lang="ts">
	import { tick } from 'svelte';
	import SettingsGroup from '#lib/components/SettingsGroup.svelte';
	import SettingsFeedback from '#lib/components/SettingsFeedback.svelte';
	import { Checkbox } from '#lib/components/ui/checkbox/index.ts';
	import { Button } from '#lib/components/ui/button/index.ts';
	import { Input } from '#lib/components/ui/input/index.ts';
	import { Label } from '#lib/components/ui/label/index.ts';
	import { t } from '#lib/i18n.ts';
	import { useSettings } from '#lib/settings.svelte.ts';
	import DomainImport from './DomainImport.svelte';
	import DomainList from './DomainList.svelte';
	const model = useSettings();
	let input = $state('');
	let includeSubdomains = $state(true);
	let domainInput = $state<HTMLInputElement | null>(null);
	const disabled = $derived(model.changingDomains || !model.state?.enabled);

	async function add(event: SubmitEvent) {
		event.preventDefault();
		if (await model.addDomain(input, includeSubdomains)) input = '';
		await tick();
		domainInput?.focus();
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
		<div class="scope-control">
			<Checkbox id="include-subdomains" bind:checked={includeSubdomains} {disabled} />
			<Label for="include-subdomains" class="font-normal text-muted-foreground"
				>{t('includeSubdomains')}</Label
			>
		</div>
	</form>
	<SettingsFeedback
		id="domain-feedback"
		message={model.domainFeedback}
		error={model.domainFeedbackError}
		inset
	/>
	<DomainImport />
	<DomainList onremoved={() => domainInput?.focus()} />
</SettingsGroup>

<style>
	@layer components {
		#domain-form {
			padding: 16px;
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
	}
</style>
