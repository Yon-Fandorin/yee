<script lang="ts">
	import { tick } from 'svelte';
	import type { FilterSubscription } from '#lib/bridge.ts';
	import { Button } from '#lib/components/ui/button/index.ts';
	import { Switch } from '#lib/components/ui/switch/index.ts';
	import { locale, t } from '#lib/i18n.ts';
	import { useSettings } from '#lib/settings.svelte.ts';
	let {
		list,
		disabled,
		onremoved
	}: {
		list: FilterSubscription;
		disabled: boolean;
		onremoved: () => void;
	} = $props();
	const model = useSettings();
	let enabled = $state(false);
	let toggle = $state<HTMLButtonElement | null>(null);
	$effect(() => {
		enabled = list.enabled;
	});

	async function change(value: boolean) {
		await model.changeSubscription(list.url, value);
		enabled = list.enabled;
		await tick();
		toggle?.focus();
	}
	async function remove() {
		await model.removeSubscription(list.url);
		await tick();
		onremoved();
	}
</script>

<li>
	<div class="list-detail">
		<span class="list-title">{list.title}</span>
		<span class="list-url">{list.url}</span>
		<span class="hint"
			>{t(list.enabled ? 'subscriptionEnabled' : 'subscriptionDisabled')}
			{#if list.checkedAt}
				· {t('lastChecked', new Date(list.checkedAt).toLocaleString(locale))}{/if}
		</span>
		{#if list.updateFailed}<span class="error">{t('subscriptionRetained')}</span>{/if}
	</div>
	<Switch
		bind:ref={toggle}
		bind:checked={enabled}
		{disabled}
		aria-label={t('subscriptionEnabledLabel', list.title)}
		onCheckedChange={change}
	/>
	<Button
		variant="ghost"
		class="text-muted-foreground"
		{disabled}
		aria-label={t('removeSubscriptionLabel', list.title)}
		onclick={remove}>{t('removeDomain')}</Button
	>
</li>

<style>
	@layer components {
		li {
			display: flex;
			align-items: center;
			gap: 12px;
			padding: 14px 16px;
		}
		li:not(:first-child) {
			border-top: 1px solid var(--border);
		}
		.list-detail {
			min-width: 0;
			flex: 1;
			display: grid;
			gap: 4px;
		}
		.list-title {
			font-size: 13px;
			font-weight: 500;
			overflow-wrap: anywhere;
		}
		.list-url {
			color: var(--muted-foreground);
			font-size: 11px;
			overflow-wrap: anywhere;
		}
		.hint {
			color: var(--muted-foreground);
			font-size: 12px;
			line-height: 1.6;
		}
		.error {
			color: var(--destructive);
			font-size: 12px;
		}
	}
</style>
