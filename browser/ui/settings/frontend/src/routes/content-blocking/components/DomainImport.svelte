<script lang="ts">
	import { onDestroy, tick } from 'svelte';
	import { Button } from '#lib/components/ui/button/index.ts';
	import type { DomainImportFormat, DomainImportPreview } from '#lib/bridge.ts';
	import { domainImportMaxBytes, t } from '#lib/i18n.ts';
	import { useSettings } from '#lib/settings.svelte.ts';
	const model = useSettings();
	let fileInput = $state<HTMLInputElement | null>(null);
	let importButton = $state<HTMLButtonElement | null>(null);
	let fileText = $state('');
	let format = $state<DomainImportFormat>('csv');
	let filename = $state('');
	let preview = $state<DomainImportPreview>();
	let previewRules = $state('');
	let reading = $state(false);
	let page = $state(0);
	let readEpoch = 0;
	const pageSize = 50;
	const pageCount = $derived(Math.max(1, Math.ceil((preview?.rows.length ?? 0) / pageSize)));
	const disabled = $derived(reading || model.changingDomains || !model.state?.enabled);
	const currentRules = $derived(JSON.stringify(model.state?.blockedDomains ?? []));
	const previewStale = $derived(Boolean(preview && previewRules !== currentRules));
	const statusKeys = {
		add: 'domainImportAdd',
		duplicate: 'domainImportDuplicate',
		'invalid-domain': 'invalidDomain',
		'invalid-columns': 'domainImportInvalidColumns',
		'invalid-scope': 'domainImportInvalidScope'
	};
	onDestroy(() => ++readEpoch);
	$effect(() => {
		if (previewStale && !disabled) void refreshPreview();
	});

	function clear() {
		++readEpoch;
		preview = undefined;
		previewRules = '';
		fileText = '';
		filename = '';
		page = 0;
		if (fileInput) fileInput.value = '';
	}

	async function cancel() {
		clear();
		await tick();
		importButton?.focus();
	}

	async function updatePreview(
		text: string,
		nextFormat: DomainImportFormat,
		nextFilename: string,
		epoch: number
	) {
		const rules = currentRules;
		const next = await model.previewDomains(nextFormat, text);
		if (epoch !== readEpoch) return;
		format = nextFormat;
		fileText = text;
		filename = nextFilename;
		preview = next;
		previewRules = rules;
		page = 0;
	}

	async function refreshPreview() {
		const epoch = ++readEpoch;
		reading = true;
		try {
			await updatePreview(fileText, format, filename, epoch);
		} finally {
			if (epoch === readEpoch) reading = false;
		}
	}

	async function readFile(event: Event) {
		const file = (event.currentTarget as HTMLInputElement).files?.[0];
		if (!file || disabled) return;
		clear();
		const epoch = readEpoch;
		reading = true;
		try {
			if (file.size > domainImportMaxBytes) throw new Error('domainImportTooLarge');
			const extension = file.name.toLowerCase().split('.').at(-1);
			if (extension !== 'csv' && extension !== 'txt') throw new Error('domainImportInvalidFile');
			const text = new TextDecoder('utf-8', { fatal: true }).decode(await file.arrayBuffer());
			if (epoch !== readEpoch) return;
			await updatePreview(text, extension, file.name, epoch);
		} catch (error) {
			if (epoch === readEpoch) {
				model.domainFeedback = t(
					error instanceof Error && error.message === 'domainImportTooLarge'
						? 'domainImportTooLarge'
						: 'domainImportInvalidFile'
				);
				model.domainFeedbackError = true;
			}
		} finally {
			if (epoch === readEpoch) reading = false;
		}
	}

	async function apply() {
		if (!preview || disabled || previewStale || !preview.additions) return;
		// Other settings tabs can change saved rules; submit only approved additions.
		const rows = preview.rows
			.filter((row) => row.status === 'add')
			.map((row) => `${row.domain},${row.includeSubdomains}`);
		reading = true;
		try {
			if (await model.importDomains('csv', 'domain,include_subdomains\r\n' + rows.join('\r\n'))) {
				reading = false;
				await cancel();
			}
		} finally {
			reading = false;
		}
	}

	function download(text: string, name: string) {
		const url = URL.createObjectURL(
			new Blob(['\uFEFF' + text], { type: 'text/csv;charset=utf-8' })
		);
		const link = document.createElement('a');
		link.href = url;
		link.download = name;
		link.click();
		setTimeout(() => URL.revokeObjectURL(url), 1000);
	}

	function exportDomains() {
		const rows =
			model.state?.blockedDomains.map((rule) => `${rule.domain},${rule.includeSubdomains}`) ?? [];
		download('domain,include_subdomains\r\n' + rows.join('\r\n') + '\r\n', 'blocked-domains.csv');
	}
</script>

<div class="file-actions">
	<input
		id="domain-file"
		type="file"
		bind:this={fileInput}
		accept=".csv,.txt"
		hidden
		{disabled}
		onchange={readFile}
	/>
	<div class="buttons">
		<Button
			id="import-domains"
			bind:ref={importButton}
			variant="outline"
			{disabled}
			onclick={() => fileInput?.click()}
		>
			{t(reading ? 'readingDomainFile' : 'importDomains')}
		</Button>
		<Button
			id="export-domains"
			variant="ghost"
			disabled={disabled || !model.state?.blockedDomains.length}
			onclick={exportDomains}>{t('exportDomains')}</Button
		>
		<Button
			id="domain-template"
			variant="ghost"
			{disabled}
			onclick={() =>
				download(
					'domain,include_subdomains\r\nads.example.com,true\r\n',
					'blocked-domains-template.csv'
				)}>{t('downloadDomainTemplate')}</Button
		>
	</div>
	<p id="domain-file-hint">{t('domainFileHint')}</p>
</div>

{#if preview}
	<section id="domain-preview" aria-labelledby="domain-preview-title">
		<h3 id="domain-preview-title">{t('domainPreviewTitle')}</h3>
		<p class="filename">{filename}</p>
		<p id="domain-preview-summary" role="status">
			{t(
				'domainPreviewSummary',
				String(preview.additions),
				String(preview.duplicates),
				String(preview.invalid)
			)}
		</p>
		<div class="preview-table">
			<table>
				<thead
					><tr
						><th>{t('previewLineColumn')}</th><th>{t('domainAddress')}</th>
						<th>{t('previewStatusColumn')}</th></tr
					></thead
				><tbody>
					{#each preview.rows.slice(page * pageSize, (page + 1) * pageSize) as row}
						<tr data-status={row.status}
							><td>{row.line}</td><td
								>{row.domain}
								{#if row.status === 'add'}<span class="scope"
										>{t(row.includeSubdomains ? 'domainAndSubdomains' : 'domainExact')}</span
									>{/if}
							</td><td>{t(statusKeys[row.status])}</td></tr
						>
					{/each}
				</tbody>
			</table>
		</div>
		{#if pageCount > 1}<div class="pagination">
				<Button variant="ghost" disabled={page === 0 || disabled} onclick={() => --page}
					>{t('previousPage')}</Button
				>
				<span>{page + 1} / {pageCount}</span>
				<Button variant="ghost" disabled={page === pageCount - 1 || disabled} onclick={() => ++page}
					>{t('nextPage')}</Button
				>
			</div>{/if}
		<div class="buttons preview-actions">
			<Button
				id="apply-domain-import"
				disabled={disabled || previewStale || !preview.additions}
				onclick={apply}
			>
				{t('applyDomainImport', String(preview.additions))}</Button
			>
			<Button id="cancel-domain-import" variant="ghost" {disabled} onclick={cancel}
				>{t('cancelDomainImport')}</Button
			>
		</div>
	</section>
{/if}

<style>
	@layer components {
		.file-actions {
			padding: 0 16px 16px;
		}
		.buttons {
			display: flex;
			flex-wrap: wrap;
			align-items: center;
			gap: 6px;
		}
		p {
			color: var(--muted-foreground);
			font-size: 12px;
			margin-top: 9px;
		}
		#domain-preview {
			padding: 16px;
			border-top: 1px solid var(--border);
		}
		h3 {
			font-size: 12px;
			font-weight: 550;
			margin: 0;
		}
		.filename {
			overflow-wrap: anywhere;
		}
		.preview-table {
			margin-top: 12px;
			max-height: 300px;
			overflow: auto;
			border: 1px solid var(--border);
			border-radius: 5px;
		}
		table {
			width: 100%;
			border-collapse: collapse;
			table-layout: fixed;
			font-size: 12px;
		}
		th,
		td {
			padding: 8px;
			text-align: start;
			vertical-align: top;
			overflow-wrap: anywhere;
		}
		th {
			background: var(--muted);
			font-weight: 500;
		}
		th:first-child,
		td:first-child {
			width: 42px;
			color: var(--muted-foreground);
		}
		th:last-child {
			width: 42%;
		}
		tr + tr td {
			border-top: 1px solid var(--border);
		}
		tr[data-status^='invalid'] td:last-child {
			color: var(--destructive);
		}
		.scope {
			display: block;
			color: var(--muted-foreground);
			margin-top: 4px;
		}
		.preview-actions {
			margin-top: 14px;
		}
		.pagination {
			display: flex;
			justify-content: space-between;
			align-items: center;
			margin-top: 8px;
			font-size: 12px;
			color: var(--muted-foreground);
		}
	}
</style>
