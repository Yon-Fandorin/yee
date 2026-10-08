<script lang="ts">
	import SettingsFeedback from '#lib/components/SettingsFeedback.svelte';
	import SettingsGroup from '#lib/components/SettingsGroup.svelte';
	import Icon from '#lib/components/Icon.svelte';
	import { Button } from '#lib/components/ui/button/index.ts';
	import { Input } from '#lib/components/ui/input/index.ts';
	import { Label } from '#lib/components/ui/label/index.ts';
	import { t } from '#lib/i18n.ts';
	import { useSettings } from '#settings/settings.svelte.ts';
	const model = useSettings();
	let input = $state('');
	let siteInput = $state<HTMLInputElement | null>(null);
	async function add(event: SubmitEvent) {
		event.preventDefault();
		if (await model.changeException(input, false)) input = '';
		siteInput?.focus();
	}
	async function remove(host: string) {
		await model.changeException(host, true);
		siteInput?.focus();
	}
</script>

<SettingsGroup title={t('exceptionsTitle')} description={t('exceptionsDescription')}>
	<form id="exception-form" onsubmit={add}>
		<Label for="site-input">{t('siteAddress')}</Label>
		<div class="input-row">
			<Input
				id="site-input"
				bind:ref={siteInput}
				bind:value={input}
				class="h-8 bg-background text-xs md:text-xs"
				placeholder={t('sitePlaceholder')}
				required
				maxlength={2048}
				autocomplete="off"
				spellcheck="false"
				aria-describedby="site-hint"
				disabled={model.changingException || !model.state?.enabled}
			/><Button
				id="add-exception"
				type="submit"
				size="lg"
				disabled={model.changingException || !model.state?.enabled}>{t('addException')}</Button
			>
		</div>
	</form>
	{#if model.state?.exceptions.length}
		<ul id="exceptions" class="exception-list" aria-label={t('exceptionListLabel')}>
			{#each model.state.exceptions as host (host)}<li>
					<span class="site-icon"><Icon name="globe" size={15} /></span><span class="site-host"
						>{host}</span
					><Button
						variant="ghost"
						class="text-muted-foreground"
						aria-label={t('removeExceptionLabel', host)}
						disabled={model.changingException || !model.state?.enabled}
						onclick={() => remove(host)}>{t('removeException')}</Button
					>
				</li>{/each}
		</ul>
	{:else if model.state}
		<div id="exceptions-empty" class="empty">
			<strong>{t('noExceptions')}</strong>
			<p>{t('noExceptionsHint')}</p>
		</div>
	{/if}
</SettingsGroup>
<SettingsFeedback
	id="exception-feedback"
	message={model.exceptionFeedback}
	error={model.exceptionFeedbackError}
/>
<p id="site-hint" class="detail">{t('siteHint')}</p>

<style>
	@layer components {
		#exception-form {
			padding: 16px;
		}
		.input-row {
			display: flex;
			align-items: center;
			gap: 8px;
			margin-top: 9px;
		}
		.empty {
			padding: 23px 20px 25px;
			text-align: center;
			border-top: 1px solid var(--border);
		}
		.empty strong {
			font-weight: 500;
			font-size: 12px;
		}
		.empty p {
			margin-top: 4px;
			color: var(--muted-foreground);
			font-size: 12px;
		}
		.exception-list {
			list-style: none;
			padding: 0;
			margin: 0;
			border-top: 1px solid var(--border);
		}
		.exception-list li {
			display: flex;
			align-items: center;
			gap: 10px;
			min-height: 48px;
			padding: 8px 16px;
		}
		.exception-list li + li {
			border-top: 1px solid var(--border);
		}
		.site-icon {
			display: flex;
			color: var(--muted-foreground);
			flex: none;
		}
		.site-host {
			min-width: 0;
			flex: 1;
			overflow-wrap: anywhere;
			font-size: 12px;
		}
		.detail {
			margin-top: 9px;
			color: var(--muted-foreground);
			font-size: 12px;
		}
	}
</style>
