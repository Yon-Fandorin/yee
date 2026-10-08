<script lang="ts">
	import Folder from '@lucide/svelte/icons/folder';
	import Globe from '@lucide/svelte/icons/globe';
	import Ellipsis from '@lucide/svelte/icons/ellipsis';
	import { Button } from '#lib/components/ui/button/index.ts';
	import { Checkbox } from '#lib/components/ui/checkbox/index.ts';
	import { Badge } from '#lib/components/ui/badge/index.ts';
	import * as DropdownMenu from '#lib/components/ui/dropdown-menu/index.ts';
	import Hint from '#lib/components/Hint.svelte';
	import { t } from '#lib/i18n.ts';
	import type { BookmarkNode, BookmarksModel } from '../bookmarks.svelte.ts';
	let { node, model }: { node: BookmarkNode; model: BookmarksModel } = $props();
	const title = $derived(node.title || t('bookmarksUntitled'));
	const canModify = $derived(model.canModify(node));
	const source = $derived.by(() => {
		try {
			const url = new URL(node.url ?? '');
			return ['http:', 'https:'].includes(url.protocol) ? url : null;
		} catch {
			return null;
		}
	});
	const favicon = $derived(
		source ? `chrome://favicon2/?size=32&pageUrl=${encodeURIComponent(source.href)}` : ''
	);
	let imageFailed = $state(false);
</script>

<li class="bookmark-row" data-bookmark-id={node.id}>
	{#if canModify}
		<Checkbox
			checked={model.selected.includes(node.id)}
			disabled={model.busy}
			aria-label={t('bookmarksSelect', title)}
			onCheckedChange={(checked) => model.toggleSelected(node.id, checked)}
		/>
	{:else}<span class="selection-space"></span>{/if}
	<div class="item-icon">
		{#if !node.url}<Folder size={18} strokeWidth={1.5} aria-hidden="true" />
		{:else if favicon && !imageFailed}<img
				src={favicon}
				alt=""
				onerror={() => (imageFailed = true)}
			/>
		{:else}<Globe size={17} strokeWidth={1.5} aria-hidden="true" />{/if}
	</div>
	<div class="details">
		<Hint text={node.url || title}>
			{#snippet children({ props })}
				<Button
					{...props}
					variant="ghost"
					class="item-title block h-auto p-0 text-[13px]/relaxed font-[450]"
					onclick={() => model.open(node)}
				>
					<span>{title}</span>
				</Button>
			{/snippet}
		</Hint>
		<div class="metadata">
			{#if node.url}<span>{source?.hostname || node.url}</span>
			{:else}<span>{t('bookmarksFolder')}</span>{/if}
			{#if model.query.trim() && node.parentId && model.nodes[node.parentId]}
				<Button
					variant="link"
					class="location h-auto p-0 text-[length:inherit]/relaxed text-inherit"
					onclick={() => model.navigate(node.parentId!)}
				>
					{model.folderPath(model.nodes[node.parentId])}
				</Button>
			{/if}
		</div>
	</div>
	{#if model.isManaged(node)}<Badge variant="secondary">{t('bookmarksManaged')}</Badge>{/if}
	<DropdownMenu.Root>
		<Hint text={t('bookmarksActions', title)}>
			{#snippet children({ props: tooltipProps })}
				<DropdownMenu.Trigger {...tooltipProps}>
					{#snippet child({ props })}
						<Button
							{...props}
							variant="ghost"
							size="icon"
							aria-label={t('bookmarksActions', title)}
						>
							<Ellipsis size={16} />
						</Button>
					{/snippet}
				</DropdownMenu.Trigger>
			{/snippet}
		</Hint>
		<DropdownMenu.Content align="end">
			<DropdownMenu.Item onclick={() => model.open(node)}>
				{t(node.url ? 'bookmarksOpen' : 'bookmarksOpenFolder')}
			</DropdownMenu.Item>
			{#if model.query.trim() && node.parentId}
				<DropdownMenu.Item onclick={() => model.navigate(node.parentId!)}
					>{t('bookmarksShowInFolder')}</DropdownMenu.Item
				>
			{/if}
			{#if canModify}
				<DropdownMenu.Separator />
				<DropdownMenu.Item
					disabled={model.busy}
					onclick={() => model.openEditor({ kind: 'edit', id: node.id })}
					>{t(node.url ? 'bookmarksEdit' : 'bookmarksRename')}</DropdownMenu.Item
				>
				<DropdownMenu.Item
					disabled={model.busy}
					onclick={() => model.openEditor({ kind: 'move', id: node.id })}
					>{t('bookmarksMove')}</DropdownMenu.Item
				>
				<DropdownMenu.Separator />
				<DropdownMenu.Item
					disabled={model.busy}
					variant="destructive"
					onclick={() => model.requestRemoval([node.id])}>{t('bookmarksDelete')}</DropdownMenu.Item
				>
			{/if}
		</DropdownMenu.Content>
	</DropdownMenu.Root>
</li>

<style>
	@layer components {
		.bookmark-row {
			display: flex;
			align-items: center;
			gap: 12px;
			min-height: 62px;
			padding: 9px 8px;
			border-bottom: 1px solid var(--border);
		}
		.selection-space {
			width: 16px;
			flex-shrink: 0;
		}
		.item-icon {
			display: grid;
			place-items: center;
			width: 30px;
			height: 30px;
			border-radius: 7px;
			flex-shrink: 0;
			color: var(--muted-foreground);
			background: var(--muted);
		}
		.item-icon img {
			width: 16px;
			height: 16px;
		}
		.details {
			flex: 1;
			min-width: 0;
		}
		.details :global(.item-title) {
			max-width: 100%;
			background: transparent;
			text-align: start;
		}
		.details :global(.item-title span) {
			display: block;
			white-space: nowrap;
			overflow: hidden;
			text-overflow: ellipsis;
		}
		.metadata {
			display: flex;
			align-items: center;
			gap: 8px;
			margin-top: 3px;
			font-size: 11px;
			color: var(--muted-foreground);
		}
		.metadata span,
		.metadata :global(.location) {
			overflow: hidden;
			white-space: nowrap;
			text-overflow: ellipsis;
		}
		.metadata :global(.location) {
			max-width: 55%;
		}
	}
</style>
