<script lang="ts">
	import { page } from '$app/state';
	import Icon from '#lib/components/Icon.svelte';
	import { Button } from '#lib/components/ui/button/index.ts';
	import { Separator } from '#lib/components/ui/separator/index.ts';
	import { nativeSettingsUrl, productName, t } from '#lib/i18n.ts';
	const blocking = $derived(page.url.pathname.replace(/\/$/, '') === '/content-blocking');
</script>

<aside class="navigation">
	<a class="brand" href="/" aria-label={t('settingsHome')}
		><span class="brand-mark"><Icon name="settings" size={17} /></span><span>{productName}</span></a
	>
	<p class="nav-heading">{t('settingsTitle')}</p>
	<nav aria-label={t('settingsTitle')}>
		<Button
			variant="ghost"
			class="h-auto min-h-[34px] justify-start gap-2.5 px-2.5 text-[13px] font-normal"
			href="/"
			aria-current={!blocking ? 'page' : undefined}
			><Icon name="settings" /><span>{t('navGeneral')}</span></Button
		>
		<Button
			variant="ghost"
			class="h-auto min-h-[34px] justify-start gap-2.5 px-2.5 text-[13px] font-normal"
			href="/content-blocking"
			aria-current={blocking ? 'page' : undefined}
			><Icon name="shield" /><span>{t('navBlocking')}</span></Button
		>
	</nav>
	<div class="navigation-footer">
		<Separator class="navigation-divider" />
		<Button
			variant="ghost"
			class="advanced-link w-full justify-start gap-2.5 px-2.5 font-normal text-muted-foreground"
			href={nativeSettingsUrl}
			><Icon name="external" size={14} /><span>{t('advancedSettings')}</span></Button
		>
	</div>
</aside>

<style>
	@layer components {
		.navigation {
			min-height: 0;
			overflow-y: auto;
			display: flex;
			flex-direction: column;
			padding: 28px 14px 22px;
			background: var(--sidebar);
			border-inline-end: 1px solid var(--sidebar-border);
		}
		.navigation > * {
			flex-shrink: 0;
		}
		.brand {
			display: flex;
			align-items: center;
			gap: 9px;
			padding: 0 10px;
			margin-bottom: 34px;
			font-size: 15px;
			font-weight: 600;
			letter-spacing: -0.3px;
		}
		.brand > span:last-child {
			min-width: 0;
			overflow-wrap: anywhere;
		}
		.brand-mark {
			display: grid;
			place-items: center;
			width: 25px;
			height: 25px;
			flex: none;
			background: var(--sidebar-accent);
			border-radius: 6px;
		}
		.nav-heading {
			margin: 0 10px 9px;
			color: var(--muted-foreground);
			font-size: 12px;
		}
		nav {
			display: grid;
			gap: 3px;
		}
		nav :global(a) {
			justify-content: start;
			gap: 9px;
			min-height: 34px;
			padding-inline: 10px;
			color: var(--muted-foreground);
			font-size: 13px;
			font-weight: 400;
		}
		nav :global(a[aria-current='page']) {
			background: var(--sidebar-accent);
			color: var(--sidebar-accent-foreground);
			font-weight: 500;
		}
		.navigation-footer {
			margin-top: auto;
			padding-top: 20px;
		}
		.navigation-footer :global(.navigation-divider) {
			margin-bottom: 12px;
		}
		.navigation-footer :global(.advanced-link) {
			justify-content: start;
			gap: 9px;
			padding-inline: 10px;
			color: var(--muted-foreground);
			font-weight: 400;
		}
		@media (max-width: 780px) {
			.navigation {
				padding-inline: 10px;
			}
		}
		@media (max-width: 540px) {
			.navigation {
				max-height: 50dvh;
				padding: 18px 18px 12px;
				border-inline-end: 0;
				border-bottom: 1px solid var(--sidebar-border);
			}
			.brand {
				margin-bottom: 16px;
				padding: 0;
			}
			.nav-heading {
				display: none;
			}
			nav {
				display: flex;
				flex-wrap: wrap;
				gap: 4px;
			}
			.navigation-footer {
				padding-top: 8px;
			}
			.navigation-footer :global(.navigation-divider) {
				display: none;
			}
		}
	}
</style>
