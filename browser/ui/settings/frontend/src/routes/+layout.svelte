<script lang="ts">
	// Establish Tailwind's layer order before importing component styles.
	import '#styles';
	import { onMount, type Snippet } from 'svelte';
	import { Button } from '#lib/components/ui/button/index.ts';
	import SettingsNotice from '#lib/components/SettingsNotice.svelte';
	import { productName, t } from '#lib/i18n.ts';
	import { provideSettings } from '#lib/settings.svelte.ts';
	import SettingsNavigation from './components/SettingsNavigation.svelte';
	import { manageSettingsScroll } from './settings-scroll.svelte.ts';

	let { children }: { children: Snippet } = $props();
	let main = $state<HTMLElement | null>(null);
	const model = provideSettings();
	onMount(() => model.start());
	manageSettingsScroll(
		() => main,
		() => Boolean(model.state || model.loadError)
	);
</script>

<svelte:head><title>{t('pageTitle', productName)}</title></svelte:head>
<a class="skip-link" href="#main">{t('skipToContent')}</a>
<div class="settings-shell">
	<SettingsNavigation />
	<main id="main" bind:this={main} tabindex="-1">
		<div class="page-content">
			{@render children()}
			{#if model.loadError}
				<SettingsNotice id="load-error" message={t('loadFailed')} error role="alert">
					{#snippet actions()}<Button variant="outline" onclick={() => model.refresh()}
							>{t('retry')}</Button
						>{/snippet}
				</SettingsNotice>
			{/if}
			{#if model.feedback}<p
					id="feedback"
					class:error={model.feedbackError}
					role="status"
					aria-live="polite"
				>
					{model.feedback}
				</p>{/if}
		</div>
	</main>
</div>

<style>
	@layer components {
		.settings-shell {
			display: grid;
			grid-template-columns: 218px minmax(0, 1fr);
			grid-template-rows: minmax(0, 1fr);
			height: 100dvh;
			overflow: hidden;
		}
		main {
			padding: 54px 48px 72px;
			min-width: 0;
			min-height: 0;
			overflow-y: auto;
			overscroll-behavior-y: contain;
		}
		main:focus-visible {
			outline-offset: -4px;
		}
		.page-content {
			width: 100%;
			max-width: 680px;
			margin-inline: auto;
		}
		#feedback {
			margin-top: 20px;
			font-size: 12px;
			color: var(--muted-foreground);
		}
		.error {
			color: var(--destructive);
		}
		.skip-link {
			position: fixed;
			inset-block-start: -60px;
			inset-inline-start: 16px;
			padding: 9px 14px;
			background: var(--background);
			z-index: 2;
		}
		.skip-link:focus {
			inset-block-start: 12px;
		}
		@media (max-width: 780px) {
			.settings-shell {
				grid-template-columns: 180px minmax(0, 1fr);
			}
			main {
				padding: 38px 24px 50px;
			}
		}
		@media (max-width: 540px) {
			.settings-shell {
				grid-template-columns: minmax(0, 1fr);
				grid-template-rows: auto minmax(0, 1fr);
			}
			main {
				padding: 28px 18px 40px;
			}
		}
	}
</style>
