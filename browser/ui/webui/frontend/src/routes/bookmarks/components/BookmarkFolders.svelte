<script lang="ts">
	import ChevronRight from '@lucide/svelte/icons/chevron-right';
	import Folder from '@lucide/svelte/icons/folder';
	import { mergeProps } from 'bits-ui';
	import { Button } from '#lib/components/ui/button/index.ts';
	import Hint from '#lib/components/Hint.svelte';
	import { t } from '#lib/i18n.ts';
	import type { BookmarkNode, BookmarksModel } from '../bookmarks.svelte.ts';
	let { model }: { model: BookmarksModel } = $props();
</script>

{#snippet folders(nodes: BookmarkNode[], depth: number)}
	<ul>
		{#each nodes.filter((node) => !node.url) as node (node.id)}
			{@const hasChildren = node.children?.some((child) => !child.url)}
			{@const expanded = model.expanded.includes(node.id)}
			<li>
				<div class="folder-row" style:--depth={depth} class:active={model.folderId === node.id}>
					{#if hasChildren}
						<Hint
							text={t(expanded ? 'bookmarksCollapseFolder' : 'bookmarksExpandFolder', node.title)}
						>
							{#snippet children({ props })}
								<Button
									{...mergeProps(props, { onclick: () => model.toggleFolder(node.id) })}
									variant="ghost"
									size="icon-xs"
									class="folder-toggle"
									aria-label={t(
										expanded ? 'bookmarksCollapseFolder' : 'bookmarksExpandFolder',
										node.title
									)}
									aria-expanded={expanded}
								>
									<ChevronRight size={12} class={expanded ? 'rotate-90' : ''} />
								</Button>
							{/snippet}
						</Hint>
					{:else}<span class="disclosure-space"></span>{/if}
					<Button
						variant="ghost"
						class="folder-link h-auto flex-1 justify-start gap-[7px] px-1 py-[7px]"
						aria-current={model.folderId === node.id ? 'page' : undefined}
						onclick={() => model.navigate(node.id)}
					>
						<Folder size={14} aria-hidden="true" />
						<span>{node.title || t('bookmarksUntitled')}</span>
					</Button>
				</div>
				{#if expanded && hasChildren}{@render folders(node.children ?? [], depth + 1)}{/if}
			</li>
		{/each}
	</ul>
{/snippet}

<nav aria-label={t('bookmarksFolders')} class="folders">
	{#if model.hasMultipleStores}
		{#each [true, false] as syncing}
			<div class="folder-group">
				<h2>{t(syncing ? 'bookmarksAccount' : 'bookmarksDevice')}</h2>
				{@render folders(
					model.roots.filter((node) => node.syncing === syncing && !model.isManaged(node)),
					0
				)}
			</div>
		{/each}
		{@render folders(
			model.roots.filter((node) => model.isManaged(node)),
			0
		)}
	{:else}{@render folders(model.roots, 0)}{/if}
</nav>

<style>
	@layer components {
		.folders {
			min-width: 0;
		}
		.folder-group {
			margin-bottom: 16px;
		}
		h2 {
			margin: 0 0 5px;
			padding-inline-start: 24px;
			font-size: 11px;
			font-weight: 450;
			color: var(--muted-foreground);
		}
		ul {
			list-style: none;
			margin: 0;
			padding: 0;
		}
		.folder-row {
			display: flex;
			align-items: center;
			gap: 2px;
			padding-inline-start: calc(min(var(--depth), 5) * 12px);
			border-radius: 6px;
			color: var(--muted-foreground);
		}
		.folder-row.active {
			color: var(--foreground);
			background: var(--muted);
		}
		.folder-row :global(.folder-link) {
			min-width: 0;
			text-align: start;
		}
		.folder-row :global(.folder-link span) {
			overflow: hidden;
			text-overflow: ellipsis;
			white-space: nowrap;
		}
		.disclosure-space {
			width: 20px;
			flex-shrink: 0;
		}
		@media (max-width: 700px) {
			.folder-row :global(.folder-toggle),
			.disclosure-space {
				display: none;
			}
			.folders {
				display: flex;
				gap: 12px;
				overflow-x: auto;
				padding-bottom: 10px;
				border-bottom: 1px solid var(--border);
			}
			.folders > ul,
			.folder-group > ul {
				display: flex;
				gap: 4px;
			}
			.folders > ul > li,
			.folder-group > ul > li {
				flex-shrink: 0;
			}
			.folders > ul > li > ul,
			.folder-group > ul > li > ul {
				display: none;
			}
		}
	}
</style>
