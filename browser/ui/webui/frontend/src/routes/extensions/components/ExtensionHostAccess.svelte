<script lang="ts">
	import X from '@lucide/svelte/icons/x';
	import { Button } from '#lib/components/ui/button/index.ts';
	import { Input } from '#lib/components/ui/input/index.ts';
	import { Label } from '#lib/components/ui/label/index.ts';
	import { Switch } from '#lib/components/ui/switch/index.ts';
	import * as NativeSelect from '#lib/components/ui/native-select/index.ts';
	import * as Dialog from '#lib/components/ui/dialog/index.ts';
	import Hint from '#lib/components/Hint.svelte';
	import SettingsRow from '#lib/components/SettingsRow.svelte';
	import { t } from '#lib/i18n.ts';
	import type { ExtensionInfo, ExtensionsModel, HostAccess } from '../extensions.svelte.ts';
	let { item, model }: { item: ExtensionInfo; model: ExtensionsModel } = $props();
	const permissions = $derived(item.permissions.runtimeHostPermissions);
	const granted = $derived(permissions?.hosts.filter((entry) => entry.granted) ?? []);
	let selectedAccess = $derived(permissions?.hostAccess ?? 'ON_CLICK');
	let adding = $state(false);
	let site = $state('');
	const disabled = $derived(!!model.busy);
	function openAddSite() {
		model.error = '';
		site = '';
		adding = true;
	}
	function chooseAccess(select: HTMLSelectElement) {
		const value = select.value;
		// 네이티브 권한이 바뀌기 전에는 바인딩과 DOM 모두 기존 값을 표시한다.
		selectedAccess = permissions?.hostAccess ?? 'ON_CLICK';
		select.value = selectedAccess;
		if (value === 'ON_SPECIFIC_SITES' && permissions?.hostAccess !== 'ON_SPECIFIC_SITES')
			openAddSite();
		else void model.setHostAccess(item, value as HostAccess);
	}
	async function addSite(event: SubmitEvent) {
		event.preventDefault();
		if (await model.addHost(item, site)) {
			adding = false;
			site = '';
		}
	}
</script>

{#if permissions}
	<section class="site-access" aria-labelledby="extension-site-access-title">
		<div class="access-heading">
			<div>
				<h2 id="extension-site-access-title">{t('extensionsSiteAccess')}</h2>
				<p>{t('extensionsSiteAccessDescription')}</p>
			</div>
			{#if permissions.hasAllHosts && model.supportsHostEditing}
				<NativeSelect.Root
					bind:value={selectedAccess}
					{disabled}
					onchange={(event) => chooseAccess(event.currentTarget)}
					aria-label={t('extensionsSiteAccess')}
					data-extension-site-access
				>
					<NativeSelect.Option value="ON_CLICK">{t('extensionsOnClick')}</NativeSelect.Option>
					<NativeSelect.Option value="ON_SPECIFIC_SITES"
						>{t('extensionsSpecificSites')}</NativeSelect.Option
					>
					<NativeSelect.Option value="ON_ALL_SITES">{t('extensionsAllSites')}</NativeSelect.Option>
				</NativeSelect.Root>
			{/if}
		</div>
		{#if !model.supportsHostEditing}
			<p class="access-note">{t('extensionsNativeSiteAccessDescription')}</p>
			<Button href={model.nativeURL(item)} variant="outline">{t('extensionsNativeDetails')}</Button>
		{:else if permissions.hasAllHosts}
			<p class="access-note">{t('extensionsSiteAccessChangeNote')}</p>
			{#if permissions.hostAccess === 'ON_SPECIFIC_SITES'}
				<ul aria-label={t('extensionsGrantedSites')}>
					{#each granted as entry (entry.host)}
						<li class="host">
							<span>{entry.host}</span><Hint text={t('extensionsRemoveSite', entry.host)}
								>{#snippet children({ props })}<Button
										{...props}
										variant="ghost"
										size="icon-sm"
										{disabled}
										onclick={() => model.removeHost(item, entry.host)}
										aria-label={t('extensionsRemoveSite', entry.host)}
										><X size={14} strokeWidth={1.5} /></Button
									>{/snippet}</Hint
							>
						</li>
					{/each}
				</ul>
			{/if}
			{#if permissions.hostAccess !== 'ON_ALL_SITES'}<Button
					variant="outline"
					{disabled}
					onclick={openAddSite}>{t('extensionsAddSite')}</Button
				>{/if}
		{:else}
			<SettingsRow
				variant="plain"
				title={t('extensionsAllRequestedSites')}
				description={t('extensionsAllRequestedSitesDescription')}
			>
				<Switch
					checked={permissions.hostAccess === 'ON_ALL_SITES'}
					{disabled}
					aria-label={t('extensionsAllRequestedSites')}
					onCheckedChange={(value) =>
						model.setHostAccess(item, (value ? 'ON_ALL_SITES' : 'ON_SPECIFIC_SITES') as HostAccess)}
					data-extension-all-requested-sites
				/>
			</SettingsRow>
			<ul aria-label={t('extensionsRequestedSites')}>
				{#each permissions.hosts as entry (entry.host)}
					<li class="host">
						<span>{entry.host}</span><Switch
							checked={entry.granted}
							disabled={disabled || permissions.hostAccess === 'ON_ALL_SITES'}
							aria-label={t('extensionsAllowSite', entry.host)}
							onCheckedChange={(value) =>
								value ? model.addHost(item, entry.host) : model.removeHost(item, entry.host)}
						/>
					</li>
				{/each}
			</ul>
		{/if}
	</section>
{/if}

<Dialog.Root bind:open={adding}>
	<Dialog.Content
		showCloseButton={false}
		onEscapeKeydown={(event) => {
			if (model.busy) event.preventDefault();
		}}
		onInteractOutside={(event) => {
			if (model.busy) event.preventDefault();
		}}
	>
		<Dialog.Header
			><Dialog.Title>{t('extensionsAddSite')}</Dialog.Title><Dialog.Description
				>{t('extensionsAddSiteDescription')}</Dialog.Description
			></Dialog.Header
		>
		<form onsubmit={addSite}>
			<div class="site-field">
				<Label for="extension-site">{t('extensionsSite')}</Label><Input
					id="extension-site"
					bind:value={site}
					placeholder={t('extensionsSitePlaceholder')}
					required
					disabled={!!model.busy}
					aria-describedby="extension-site-help"
				/>
				<p id="extension-site-help">{t('extensionsSiteHelp')}</p>
			</div>
			{#if model.error}<p class="error" role="alert">{model.error}</p>{/if}
			<Dialog.Footer
				><Button variant="outline" disabled={!!model.busy} onclick={() => (adding = false)}
					>{t('extensionsCancel')}</Button
				><Button type="submit" disabled={!!model.busy || !site.trim()}>{t('extensionsAdd')}</Button
				></Dialog.Footer
			>
		</form>
	</Dialog.Content>
</Dialog.Root>

<style>
	@layer components {
		.site-access {
			padding: 22px 0;
			border-bottom: 1px solid var(--border);
		}
		.access-heading {
			display: flex;
			align-items: center;
			justify-content: space-between;
			gap: 20px;
		}
		h2 {
			font-size: 13px;
			font-weight: 500;
			margin: 0;
		}
		p {
			font-size: 12px;
			line-height: 1.6;
			color: var(--muted-foreground);
			margin: 5px 0 0;
		}
		.access-note {
			margin: 10px 0 12px;
		}
		ul {
			list-style: none;
			padding: 0;
			margin: 10px 0;
		}
		.host {
			display: flex;
			align-items: center;
			justify-content: space-between;
			gap: 16px;
			padding: 9px 0;
			font-size: 12px;
		}
		.host span {
			overflow-wrap: anywhere;
		}
		.site-field {
			display: grid;
			gap: 8px;
			margin: 12px 0 22px;
		}
		.site-field p {
			margin: 0;
		}
		.error {
			color: var(--destructive);
			margin-bottom: 12px;
		}
		@media (max-width: 620px) {
			.access-heading {
				flex-wrap: wrap;
				gap: 12px;
			}
		}
	}
</style>
