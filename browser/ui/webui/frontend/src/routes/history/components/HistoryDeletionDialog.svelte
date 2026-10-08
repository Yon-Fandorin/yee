<script lang="ts">
	import * as AlertDialog from '#lib/components/ui/alert-dialog/index.ts';
	import { t } from '#lib/i18n.ts';
	import type { HistoryModel } from '../history.svelte.ts';
	let { model }: { model: HistoryModel } = $props();
</script>

<AlertDialog.Root
	open={model.pendingRemoval.length > 0}
	onOpenChange={(open) => {
		if (!open) model.cancelRemoval();
	}}
>
	<AlertDialog.Content
		size="sm"
		onEscapeKeydown={(event) => {
			if (model.deleting) event.preventDefault();
		}}
	>
		<AlertDialog.Header>
			<AlertDialog.Title>{t('historyDeleteTitle')}</AlertDialog.Title>
			<AlertDialog.Description
				>{t(
					'historyDeleteDescription',
					String(model.pendingRemoval.length)
				)}</AlertDialog.Description
			>
		</AlertDialog.Header>
		<AlertDialog.Footer>
			<AlertDialog.Cancel disabled={model.deleting}>{t('cancel')}</AlertDialog.Cancel>
			<AlertDialog.Action
				variant="destructive"
				disabled={model.deleting}
				onclick={(event) => {
					event.preventDefault();
					void model.confirmRemoval();
				}}
			>
				{model.deleting ? t('historyDeleting') : t('historyDeleteConfirm')}
			</AlertDialog.Action>
		</AlertDialog.Footer>
	</AlertDialog.Content>
</AlertDialog.Root>
