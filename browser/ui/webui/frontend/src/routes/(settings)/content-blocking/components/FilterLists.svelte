<script lang="ts">
	import SettingsFeedback from '#lib/components/SettingsFeedback.svelte';
	import SettingsGroup from '#lib/components/SettingsGroup.svelte';
	import SettingsRow from '#lib/components/SettingsRow.svelte';
	import SettingsNotice from '#lib/components/SettingsNotice.svelte';
	import { Button } from '#lib/components/ui/button/index.ts';
	import { Badge } from '#lib/components/ui/badge/index.ts';
	import { locale, t } from '#lib/i18n.ts';
	import { useSettings } from '#settings/settings.svelte.ts';
	const model = useSettings();
	const checking = $derived(model.updating || !!model.state?.updating);
	const date = $derived(
		model.state?.checkedAt ? new Date(model.state.checkedAt).toLocaleString(locale) : ''
	);
</script>

<SettingsGroup title={t('filtersTitle')} description={t('filtersDescription')}>
	{#snippet actions()}<Button
			variant="outline"
			id="check-lists"
			disabled={checking || !model.state?.updatesAvailable}
			onclick={() => model.checkLists()}>{t(checking ? 'checkingLists' : 'checkLists')}</Button
		>{/snippet}
	<ul class="filter-list">
		<li>
			<SettingsRow icon="shield" title="EasyList" description={t('easyListDescription')}>
				<Badge variant="outline" class="filter-state text-[11px] font-normal text-muted-foreground"
					>{t(model.state?.runningDownloaded ? 'updatedFilter' : 'bundledFilter')}</Badge
				>
			</SettingsRow>
		</li>
		<li>
			<SettingsRow icon="lock" title="EasyPrivacy" description={t('easyPrivacyDescription')}>
				<Badge variant="outline" class="filter-state text-[11px] font-normal text-muted-foreground"
					>{t(model.state?.runningDownloaded ? 'updatedFilter' : 'bundledFilter')}</Badge
				>
			</SettingsRow>
		</li>
	</ul>
</SettingsGroup>
<SettingsFeedback id="list-feedback" message={model.listFeedback} error={model.listFeedbackError} />
<p id="list-state" class="detail">{date ? t('lastChecked', date) : t('bundledNote')}</p>
{#if model.state?.pendingRestart}<SettingsNotice
		id="restart-note"
		message={t('restartNote')}
	/>{/if}
{#if model.state && !model.state.updatesAvailable}<p id="update-restriction" class="detail">
		{t(model.state.privateProfile ? 'privateUpdateRestriction' : 'updateRestriction')}
	</p>{/if}

<style>
	@layer components {
		.filter-list {
			list-style: none;
			padding: 0;
			margin: 0;
		}
		.filter-list li + li {
			border-top: 1px solid var(--border);
		}
		.detail {
			margin-top: 9px;
			color: var(--muted-foreground);
			font-size: 12px;
		}
	}
</style>
