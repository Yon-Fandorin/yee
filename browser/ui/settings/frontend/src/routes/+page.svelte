<script lang="ts">
	import SettingsGroup from '#lib/components/SettingsGroup.svelte';
	import SettingsPageHeader from '#lib/components/SettingsPageHeader.svelte';
	import SettingLink from '#lib/components/SettingLink.svelte';
	import { nativeSettingsUrl, t } from '#lib/i18n.ts';
	import { useSettings } from '#lib/settings.svelte.ts';
	const model = useSettings();
</script>

<section id="overview" aria-labelledby="overview-title">
	<SettingsPageHeader
		titleId="overview-title"
		title={t('settingsTitle')}
		description={t('overviewDescription')}
	/>
	<SettingsGroup title={t('browsingProtection')}>
		<SettingLink
			href="/content-blocking"
			icon="shield"
			title={t('blockingTitle')}
			description={t('blockingLinkDescription')}
			value={model.state ? t(model.state.enabled ? 'protectionEnabled' : 'protectionDisabled') : ''}
		/>
	</SettingsGroup>
	<SettingsGroup title={t('browserSettings')} description={t('nativeDescription')}>
		<SettingLink
			href={nativeSettingsUrl + 'content'}
			icon="globe"
			title={t('sitePermissions')}
			description={t('sitePermissionsDescription')}
			external
		/>
		<SettingLink
			href={nativeSettingsUrl + 'privacy'}
			icon="lock"
			title={t('privacySecurity')}
			description={t('privacyDescription')}
			external
		/>
		<SettingLink
			href={nativeSettingsUrl + 'appearance'}
			icon="appearance"
			title={t('appearance')}
			description={t('appearanceDescription')}
			external
		/>
		<SettingLink
			href={nativeSettingsUrl + 'downloads'}
			icon="download"
			title={t('downloads')}
			description={t('downloadsDescription')}
			external
		/>
	</SettingsGroup>
	<p class="page-footnote">{t('appearanceHint')}</p>
</section>

<style>
	@layer components {
		.page-footnote {
			margin-top: 20px;
			color: var(--muted-foreground);
			font-size: 12px;
		}
	}
</style>
