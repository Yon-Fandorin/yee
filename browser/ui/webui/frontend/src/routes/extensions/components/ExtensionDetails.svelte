<script lang="ts">
	import ArrowLeft from '@lucide/svelte/icons/arrow-left';
	import ExternalLink from '@lucide/svelte/icons/external-link';
	import TriangleAlert from '@lucide/svelte/icons/triangle-alert';
	import { Button } from '#lib/components/ui/button/index.ts';
	import { Switch } from '#lib/components/ui/switch/index.ts';
	import { t } from '#lib/i18n.ts';
	import SettingsRow from '#lib/components/SettingsRow.svelte';
	import {
		displayState,
		enabled,
		warningMessages,
		type ExtensionInfo,
		type ExtensionsModel
	} from '../extensions.svelte.ts';
	import ExtensionIcon from './ExtensionIcon.svelte';
	import ExtensionHostAccess from './ExtensionHostAccess.svelte';
	let {
		item,
		model,
		backURL,
		onback
	}: { item: ExtensionInfo; model: ExtensionsModel; backURL: string; onback: () => void } =
		$props();
	const warnings = $derived(warningMessages(item));
	const disabled = $derived(!!model.busy);
</script>

<section data-extension-details data-extension-id={item.id} aria-labelledby="extension-detail-name">
	<Button
		href={backURL}
		variant="ghost"
		class="back"
		onclick={(event) => {
			if (event.button || event.metaKey || event.ctrlKey || event.shiftKey || event.altKey) return;
			event.preventDefault();
			onback();
		}}><ArrowLeft size={14} strokeWidth={1.5} />{t('extensionsBack')}</Button
	>
	<div class="identity">
		<ExtensionIcon src={item.iconUrl} large />
		<div>
			<h2 id="extension-detail-name" data-extension-name>{item.name}</h2>
			<p>{t('extensionsVersion', item.version)}</p>
		</div>
	</div>
	{#if item.description}<p class="description">{item.description}</p>{/if}
	{#if item.controlledInfo}<p class="managed" data-extension-policy>
			{item.controlledInfo.text || t('extensionsManaged')}
		</p>{/if}
	{#if warnings.length || !model.canToggle(item)}
		<div class="warnings" data-extension-warning>
			{#each warnings as warning}<p>
					<TriangleAlert size={14} strokeWidth={1.5} aria-hidden="true" />{warning}
				</p>{/each}
			{#if !model.canToggle(item)}<p>{t('extensionsNativeRecoveryDescription')}</p>{/if}
			<Button href={model.nativeURL(item)} variant="outline"
				>{t('extensionsNativeDetails')}<ExternalLink size={12} strokeWidth={1.5} /></Button
			>
		</div>
	{/if}
	<SettingsRow
		variant="plain"
		title={t('extensionsEnabledSetting')}
		description={displayState(item)}
		><Switch
			checked={enabled(item)}
			disabled={!!model.busy || !model.canToggle(item)}
			aria-label={t('extensionsToggle', item.name)}
			onCheckedChange={(value) => model.setEnabled(item, value)}
			data-extension-enable
			data-extension-toggle
		/></SettingsRow
	>
	<section class="permissions" aria-labelledby="extension-permissions-title">
		<h3 id="extension-permissions-title">{t('extensionsPermissions')}</h3>
		{#if item.permissions.simplePermissions.length}<ul>
				{#each item.permissions.simplePermissions as permission}<li>
						{permission.message}{#if permission.submessages.length}<ul>
								{#each permission.submessages as submessage}<li>{submessage}</li>{/each}
							</ul>{/if}
					</li>{/each}
			</ul>{:else}<p>{t('extensionsNoPermissions')}</p>{/if}
	</section>
	<ExtensionHostAccess {item} {model} />
	{#if item.incognitoAccess.isEnabled && model.profile?.isIncognitoAvailable}<SettingsRow
			variant="plain"
			title={t('extensionsIncognito')}
			description={[
				t('extensionsIncognitoWarning'),
				item.incognitoAccessPendingChange ? t('extensionsPendingChange') : ''
			]
				.filter(Boolean)
				.join(' ')}
			><Switch
				checked={item.incognitoAccess.isActive}
				{disabled}
				aria-label={t('extensionsIncognito')}
				onCheckedChange={(value) => model.setConfiguration(item, { incognitoAccess: value })}
				data-extension-incognito
			/></SettingsRow
		>{/if}
	{#if item.fileAccess.isEnabled}<SettingsRow
			variant="plain"
			title={t('extensionsFileAccess')}
			description={[
				t('extensionsFileAccessDescription'),
				item.fileAccessPendingChange ? t('extensionsPendingChange') : ''
			]
				.filter(Boolean)
				.join(' ')}
			><Switch
				checked={item.fileAccess.isActive}
				{disabled}
				aria-label={t('extensionsFileAccess')}
				onCheckedChange={(value) => model.setConfiguration(item, { fileAccess: value })}
				data-extension-file-access
			/></SettingsRow
		>{/if}
	{#if item.errorCollection.isEnabled}<SettingsRow
			variant="plain"
			title={t('extensionsCollectErrors')}
			description={t('extensionsCollectErrorsDescription')}
			><Switch
				checked={item.errorCollection.isActive}
				{disabled}
				aria-label={t('extensionsCollectErrors')}
				onCheckedChange={(value) => model.setConfiguration(item, { errorCollection: value })}
				data-extension-collect-errors
			/></SettingsRow
		>{/if}
	{#if item.optionsPage}<SettingsRow
			variant="plain"
			title={t('extensionsOptions')}
			description={t('extensionsOptionsDescription')}
			><Button
				variant="outline"
				disabled={!!model.busy || item.state !== 'ENABLED'}
				onclick={() => model.openOptions(item)}
				>{t('extensionsOpenOptions')}<ExternalLink size={12} strokeWidth={1.5} /></Button
			></SettingsRow
		>{/if}
	<SettingsRow
		variant="plain"
		title={t('extensionsAdvanced')}
		description={t('extensionsAdvancedDescription')}
		><Button href={model.nativeURL(item)} variant="ghost"
			>{t('extensionsOpenNative')}<ExternalLink size={12} strokeWidth={1.5} /></Button
		></SettingsRow
	>
	{#if item.runtimeErrors.length || item.manifestErrors.length}<SettingsRow
			variant="plain"
			title={t('extensionsErrors')}
			description={t('extensionsErrorsDescription')}
			><Button href={model.nativeURL(item, 'errors')} variant="ghost"
				>{t('extensionsViewErrors')}<ExternalLink size={12} strokeWidth={1.5} /></Button
			></SettingsRow
		>{/if}
	<dl class="identifier">
		<dt>{t('extensionsId')}</dt>
		<dd>{item.id}</dd>
	</dl>
	{#if !item.mustRemainInstalled}<div class="remove">
			<Button variant="destructive" disabled={!!model.busy} onclick={() => model.remove(item)}
				>{t('extensionsRemove')}</Button
			>
		</div>{/if}
</section>

<style>
	@layer components {
		section {
			min-width: 0;
		}
		:global(.back) {
			margin: 0 0 20px -6px;
		}
		.identity {
			display: flex;
			align-items: center;
			gap: 14px;
		}
		h2 {
			margin: 0;
			font-size: 17px;
			font-weight: 550;
			overflow-wrap: anywhere;
		}
		h3 {
			font-size: 13px;
			font-weight: 500;
		}
		h3 {
			margin: 0 0 10px;
		}
		p {
			color: var(--muted-foreground);
			font-size: 12px;
			line-height: 1.6;
			margin: 4px 0 0;
		}
		.description {
			margin: 16px 0 20px;
			max-width: 720px;
			font-size: 13px;
		}
		.managed {
			padding: 10px 0;
		}
		.warnings {
			padding: 14px 0;
			border-bottom: 1px solid var(--border);
		}
		.warnings p {
			display: flex;
			align-items: flex-start;
			gap: 7px;
			margin: 0 0 10px;
			color: var(--destructive);
		}
		.warnings :global(svg) {
			flex: none;
			margin-top: 2px;
		}
		.permissions {
			padding: 22px 0;
			border-bottom: 1px solid var(--border);
		}
		.permissions ul {
			padding-left: 18px;
			margin: 0;
			font-size: 12px;
			line-height: 1.7;
			color: var(--muted-foreground);
		}
		.identifier {
			display: flex;
			gap: 16px;
			margin: 20px 0;
			color: var(--muted-foreground);
			font-size: 11px;
		}
		.identifier dd {
			margin: 0;
			overflow-wrap: anywhere;
		}
		.remove {
			margin-top: 26px;
		}
	}
</style>
