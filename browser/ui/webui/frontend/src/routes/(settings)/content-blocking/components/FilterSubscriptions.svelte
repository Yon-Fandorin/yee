<script lang="ts">
	import { tick } from 'svelte';
	import SettingsGroup from '#lib/components/SettingsGroup.svelte';
	import SettingsFeedback from '#lib/components/SettingsFeedback.svelte';
	import { Button } from '#lib/components/ui/button/index.ts';
	import { Input } from '#lib/components/ui/input/index.ts';
	import { Label } from '#lib/components/ui/label/index.ts';
	import { t } from '#lib/i18n.ts';
	import FilterSubscriptionItem from './FilterSubscriptionItem.svelte';
	import { useSettings } from '#settings/settings.svelte.ts';
	const model = useSettings();
	let url = $state('');
	let urlInput = $state<HTMLInputElement | null>(null);
	const lists = $derived(model.state?.subscriptions ?? []);
	const disabled = $derived(
		model.changingSubscriptions || model.state?.updating || !model.state?.updatesAvailable
	);

	async function add(event: SubmitEvent) {
		event.preventDefault();
		if (await model.addSubscription(url)) url = '';
		await tick();
		urlInput?.focus();
	}
</script>

<SettingsGroup title={t('subscriptionsTitle')} description={t('subscriptionsDescription')}>
	<form id="subscription-form" onsubmit={add}>
		<Label for="subscription-url">{t('subscriptionUrl')}</Label>
		<div class="input-row">
			<Input
				id="subscription-url"
				bind:ref={urlInput}
				bind:value={url}
				class="h-8 bg-background text-xs md:text-xs"
				type="url"
				placeholder="https://example.com/filters.txt"
				required
				maxlength={2048}
				autocomplete="off"
				spellcheck="false"
				aria-describedby="subscription-hint"
				{disabled}
			/>
			<Button id="add-subscription" type="submit" size="lg" {disabled}>
				{t(model.changingSubscriptions ? 'subscriptionSaving' : 'addSubscription')}
			</Button>
		</div>
		<p id="subscription-hint" class="hint">{t('subscriptionHint')}</p>
	</form>
	<SettingsFeedback
		id="subscription-feedback"
		message={model.subscriptionFeedback}
		error={model.subscriptionFeedbackError}
		inset
	/>
	{#if lists.length}
		<ul id="filter-subscriptions" aria-label={t('subscriptionsTitle')}>
			{#each lists as list (list.url)}
				<FilterSubscriptionItem {list} {disabled} onremoved={() => urlInput?.focus()} />
			{/each}
		</ul>
	{:else if model.state}<p class="empty">{t('subscriptionsEmpty')}</p>{/if}
</SettingsGroup>

<style>
	@layer components {
		form {
			padding: 16px;
		}
		.input-row {
			display: flex;
			align-items: center;
			gap: 8px;
			margin-top: 9px;
		}
		.hint {
			color: var(--muted-foreground);
			font-size: 12px;
			line-height: 1.6;
		}
		form .hint {
			margin: 9px 0 0;
		}
		ul {
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
	}
</style>
