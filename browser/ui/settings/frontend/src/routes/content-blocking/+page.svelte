<script lang="ts">
	import SettingsPageHeader from '#lib/components/SettingsPageHeader.svelte';
	import SettingsNotice from '#lib/components/SettingsNotice.svelte';
	import { Badge } from '#lib/components/ui/badge/index.ts';
	import { t } from '#lib/i18n.ts';
	import { useSettings } from '#lib/settings.svelte.ts';
	import FilterLists from './components/FilterLists.svelte';
	import SiteExceptions from './components/SiteExceptions.svelte';
	import BlockedDomains from './components/BlockedDomains.svelte';
	const model = useSettings();
</script>

<section id="content-blocking" aria-labelledby="blocking-title">
	<SettingsPageHeader
		titleId="blocking-title"
		title={t('blockingTitle')}
		description={t('blockingDescription')}
	/>
	<Badge
		variant="secondary"
		id="protection-state"
		class="protection-state h-6 gap-2 rounded-md px-2.5 text-xs font-normal"
		data-enabled={model.state?.enabled}
		role="status"
		><span class="status-dot"></span>{t(
			model.state
				? model.state.enabled
					? 'protectionEnabled'
					: 'protectionDisabled'
				: 'protectionLoading'
		)}</Badge
	>

	{#if model.state?.privateProfile}<SettingsNotice
			id="private-note"
			message={t('privateNote')}
		/>{/if}
	<FilterLists />
	<BlockedDomains />
	<SiteExceptions />
</section>

<style>
	@layer components {
		.status-dot {
			width: 6px;
			height: 6px;
			flex: none;
			border-radius: 50%;
			background: var(--muted-foreground);
		}
		:global(.protection-state[data-enabled='true']) .status-dot {
			background: var(--status);
		}
	}
</style>
