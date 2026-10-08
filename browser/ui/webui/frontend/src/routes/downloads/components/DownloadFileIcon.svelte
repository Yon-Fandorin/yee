<script lang="ts">
	import File from '@lucide/svelte/icons/file';
	import FileImage from '@lucide/svelte/icons/file-image';
	import FileVideo from '@lucide/svelte/icons/file-video';
	import FileAudio from '@lucide/svelte/icons/file-audio';
	import FileText from '@lucide/svelte/icons/file-text';
	import FileSpreadsheet from '@lucide/svelte/icons/file-spreadsheet';
	import FileArchive from '@lucide/svelte/icons/file-archive';
	import FileCode from '@lucide/svelte/icons/file-code';
	import FileChartColumn from '@lucide/svelte/icons/file-chart-column';
	import AppWindow from '@lucide/svelte/icons/app-window';
	import ShieldAlert from '@lucide/svelte/icons/shield-alert';
	import Hint from '#lib/components/Hint.svelte';
	import { t } from '#lib/i18n.ts';

	let { fileName, needsReview }: { fileName: string; needsReview: boolean } = $props();
	// This describes the filename, not its contents or safety verdict.
	const kinds = [
		{
			key: 'downloadsTypeImage',
			icon: FileImage,
			extensions: [
				'jpg',
				'jpeg',
				'png',
				'gif',
				'webp',
				'avif',
				'svg',
				'bmp',
				'ico',
				'heic',
				'heif',
				'tif',
				'tiff'
			]
		},
		{
			key: 'downloadsTypeVideo',
			icon: FileVideo,
			extensions: ['mp4', 'webm', 'mov', 'mkv', 'avi', 'm4v', 'mpeg', 'mpg']
		},
		{
			key: 'downloadsTypeAudio',
			icon: FileAudio,
			extensions: ['mp3', 'wav', 'm4a', 'aac', 'ogg', 'opus', 'flac', 'aiff']
		},
		{
			key: 'downloadsTypeDocument',
			icon: FileText,
			extensions: ['pdf', 'txt', 'md', 'rtf', 'doc', 'docx', 'odt', 'pages', 'epub']
		},
		{
			key: 'downloadsTypeSpreadsheet',
			icon: FileSpreadsheet,
			extensions: ['csv', 'tsv', 'xls', 'xlsx', 'ods', 'numbers']
		},
		{
			key: 'downloadsTypePresentation',
			icon: FileChartColumn,
			extensions: ['ppt', 'pptx', 'odp', 'key']
		},
		{
			key: 'downloadsTypeArchive',
			icon: FileArchive,
			extensions: ['zip', 'rar', '7z', 'tar', 'gz', 'bz2', 'xz', 'tgz', 'zst']
		},
		{
			key: 'downloadsTypeCode',
			icon: FileCode,
			extensions: [
				'html',
				'htm',
				'css',
				'js',
				'ts',
				'jsx',
				'tsx',
				'json',
				'xml',
				'yaml',
				'yml',
				'py',
				'rs',
				'c',
				'cc',
				'cpp',
				'h',
				'java',
				'go',
				'sh',
				'sql',
				'svelte'
			]
		},
		{
			key: 'downloadsTypeApplication',
			icon: AppWindow,
			extensions: ['dmg', 'pkg', 'app', 'exe', 'msi', 'apk', 'deb', 'rpm', 'appimage']
		}
	];
	const kind = $derived.by(() => {
		if (needsReview) return { key: 'downloadsReviewRequired', icon: ShieldAlert };
		const dot = fileName.lastIndexOf('.');
		const extension = dot > 0 ? fileName.slice(dot + 1).toLowerCase() : '';
		return (
			kinds.find((type) => type.extensions.includes(extension)) ?? {
				key: 'downloadsTypeFile',
				icon: File
			}
		);
	});
</script>

<Hint text={t(kind.key)}>
	{#snippet children({ props })}
		<span {...props} role="img" aria-label={t(kind.key)}>
			<kind.icon size={19} strokeWidth={1.5} aria-hidden="true" />
		</span>
	{/snippet}
</Hint>
