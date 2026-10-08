<script lang="ts">
	import * as AlertDialog from '#lib/components/ui/alert-dialog/index.ts';
	import { t } from '#lib/i18n.ts';
	import type { BookmarksModel } from '../bookmarks.svelte.ts';
	let { model }: { model: BookmarksModel } = $props();
</script>

<AlertDialog.Root
	open={model.pendingRemoval.length > 0}
	onOpenChange={(open) => {
		if (!open) model.cancelRemoval();
	}}
>
	<AlertDialog.Content
		onEscapeKeydown={(event) => {
			if (model.busy) event.preventDefault();
		}}
	>
		<AlertDialog.Header>
			<AlertDialog.Title>{t('bookmarksDeleteTitle')}</AlertDialog.Title>
			<AlertDialog.Description
				>{t(
					'bookmarksDeleteDescription',
					String(model.pendingRemoval.length)
				)}</AlertDialog.Description
			>
		</AlertDialog.Header>
		{#if model.error}<p role="alert" class="text-xs text-destructive">{model.error}</p>{/if}
		<AlertDialog.Footer>
			<AlertDialog.Cancel disabled={model.busy}>{t('cancel')}</AlertDialog.Cancel>
			<AlertDialog.Action
				variant="destructive"
				disabled={model.busy}
				onclick={(event) => {
					event.preventDefault();
					void model.confirmRemoval();
				}}
			>
				{t(model.busy ? 'bookmarksDeleting' : 'bookmarksDelete')}
			</AlertDialog.Action>
		</AlertDialog.Footer>
	</AlertDialog.Content>
</AlertDialog.Root>
