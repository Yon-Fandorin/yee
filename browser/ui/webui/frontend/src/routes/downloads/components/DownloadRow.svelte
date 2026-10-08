<script lang="ts">
	import { mergeProps } from 'bits-ui';
	import FolderOpen from '@lucide/svelte/icons/folder-open';
	import X from '@lucide/svelte/icons/x';
	import { Button } from '#lib/components/ui/button/index.ts';
	import Hint from '#lib/components/Hint.svelte';
	import { locale, t } from '#lib/i18n.ts';
	import DownloadFileIcon from './DownloadFileIcon.svelte';
	import type { Data, State } from '../downloads.mojom-webui.js';
	import type { DownloadAction } from '../downloads.svelte.ts';

	let {
		item,
		states,
		allowDeletingHistory,
		disabled,
		onaction
	}: {
		item: Data;
		states: typeof State;
		allowDeletingHistory: boolean;
		disabled: boolean;
		onaction: (action: DownloadAction, item: Data) => void;
	} = $props();
	const active = $derived(item.state === states.kInProgress || item.state === states.kPaused);
	const needsReview = $derived(
		item.isDangerous ||
			item.isInsecure ||
			[
				states.kDangerous,
				states.kInsecure,
				states.kAsyncScanning,
				states.kPromptForScanning,
				states.kPromptForLocalPasswordScanning
			].includes(item.state)
	);
	const complete = $derived(item.state === states.kComplete && !needsReview);
	const canOpen = $derived(complete && !item.fileExternallyRemoved);
	const source = $derived.by(() => {
		try {
			const url = new URL(item.url ?? '');
			return url.protocol === 'https:' || url.protocol === 'http:' ? url : null;
		} catch {
			return null;
		}
	});
	const status = $derived.by(() => {
		if (needsReview) return t('downloadsReviewRequired');
		if (item.fileExternallyRemoved) return t('downloadsFileRemoved');
		if (item.progressStatusText) return item.progressStatusText;
		if (item.lastReasonText) return item.lastReasonText;
		if (item.state === states.kCancelled) return t('downloadsCancelled');
		if (item.state === states.kPaused) return t('downloadsPaused');
		if (item.total > 0n) {
			const bytes = Number(item.total);
			const unit = bytes >= 1048576 ? 'megabyte' : 'kilobyte';
			return new Intl.NumberFormat(locale, {
				style: 'unit',
				unit,
				unitDisplay: 'short',
				maximumFractionDigits: 1
			}).format(bytes / (unit === 'megabyte' ? 1048576 : 1024));
		}
		return complete ? t('downloadsComplete') : '';
	});
</script>

<li class="download-row" data-download-id={item.id}>
	<div class="file-icon" class:warning={needsReview}>
		<DownloadFileIcon fileName={item.fileName} {needsReview} />
	</div>
	<div class="details">
		<Hint text={item.fileName}>
			{#snippet children({ props })}
				{#if canOpen}
					<Button
						{...mergeProps(props, { onclick: () => onaction('open', item) })}
						variant="link"
						class="h-auto max-w-full justify-start p-0 text-left text-[13px]"
						{disabled}><span class="file-name">{item.fileName}</span></Button
					>
				{:else}<div {...props} class="file-name title">{item.fileName}</div>{/if}
			{/snippet}
		</Hint>
		<div class="metadata">
			{#if source}<Hint text={source.href}>
					{#snippet children({ props })}
						<a {...props} href={source.href} target="_blank" rel="noopener noreferrer"
							>{source.hostname}</a
						>
					{/snippet}
				</Hint>{/if}
			{#if item.otr}<span>{t('downloadsPrivate')}</span>{/if}
			<span class:warning={needsReview}>{status}</span>
		</div>
		{#if active && !needsReview}
			<progress
				value={item.percent >= 0 ? item.percent : undefined}
				max="100"
				aria-label={t('downloadsProgress')}
			></progress>
		{/if}
	</div>
	<div class="actions">
		{#if needsReview}
			<Button href="chrome://downloads/" variant="outline">{t('downloadsReview')}</Button>
		{:else}
			{#if canOpen}<Hint text={item.showInFolderText || t('downloadsShowInFolder')}>
					{#snippet children({ props })}
						<Button
							{...mergeProps(props, { onclick: () => onaction('show', item) })}
							variant="ghost"
							size="icon"
							aria-label={item.showInFolderText || t('downloadsShowInFolder')}
							{disabled}><FolderOpen size={16} /></Button
						>
					{/snippet}
				</Hint>{/if}
			{#if item.state === states.kInProgress}<Button
					variant="ghost"
					{disabled}
					onclick={() => onaction('pause', item)}>{t('downloadsPause')}</Button
				>
			{:else if item.resume}<Button
					variant="ghost"
					{disabled}
					onclick={() => onaction('resume', item)}>{t('downloadsResume')}</Button
				>{/if}
			{#if item.retry}<Button variant="ghost" {disabled} onclick={() => onaction('retry', item)}
					>{t('downloadsRetry')}</Button
				>{/if}
			{#if active}<Button variant="ghost" {disabled} onclick={() => onaction('cancel', item)}
					>{t('downloadsCancel')}</Button
				>
			{:else if allowDeletingHistory}<Hint text={t('downloadsRemove')}>
					{#snippet children({ props })}
						<Button
							{...mergeProps(props, { onclick: () => onaction('remove', item) })}
							variant="ghost"
							size="icon"
							{disabled}
							aria-label={t('downloadsRemove')}><X size={15} /></Button
						>
					{/snippet}
				</Hint>{/if}
		{/if}
	</div>
</li>

<style>
	@layer components {
		.download-row {
			display: flex;
			align-items: center;
			gap: 13px;
			padding: 16px 8px;
			border-bottom: 1px solid var(--border);
		}
		.file-icon {
			display: grid;
			place-items: center;
			width: 32px;
			height: 36px;
			flex-shrink: 0;
			color: var(--muted-foreground);
		}
		.details {
			flex: 1;
			min-width: 0;
		}
		.file-name {
			display: block;
			overflow: hidden;
			text-overflow: ellipsis;
			white-space: nowrap;
		}
		.title {
			font-weight: 500;
		}
		.metadata {
			display: flex;
			flex-wrap: wrap;
			gap: 4px 12px;
			margin-top: 4px;
			font-size: 12px;
			color: var(--muted-foreground);
		}
		.metadata a {
			max-width: 26ch;
			overflow: hidden;
			text-overflow: ellipsis;
			white-space: nowrap;
		}
		.metadata a:hover {
			text-decoration: underline;
		}
		.warning {
			color: var(--destructive);
		}
		.actions {
			display: flex;
			align-items: center;
			gap: 4px;
			flex-shrink: 0;
		}
		progress {
			display: block;
			margin-top: 9px;
			width: min(100%, 280px);
			height: 3px;
			accent-color: var(--primary);
		}
		@media (max-width: 620px) {
			.download-row {
				flex-wrap: wrap;
			}
			.details {
				flex-basis: calc(100% - 45px);
			}
			.actions {
				margin-inline-start: 45px;
			}
		}
	}
</style>
