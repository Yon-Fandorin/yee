<script lang="ts">
	import * as Dialog from '#lib/components/ui/dialog/index.ts';
	import { Button } from '#lib/components/ui/button/index.ts';
	import { Input } from '#lib/components/ui/input/index.ts';
	import { Label } from '#lib/components/ui/label/index.ts';
	import * as NativeSelect from '#lib/components/ui/native-select/index.ts';
	import { t } from '#lib/i18n.ts';
	import type { BookmarksModel, Editor } from '../bookmarks.svelte.ts';
	let { model }: { model: BookmarksModel } = $props();
	let title = $state('');
	let url = $state('');
	let destination = $state('');
	let previous: Editor | null = null;
	const editor = $derived(model.editor);
	const node = $derived(editor && 'id' in editor ? model.nodes[editor.id] : null);
	const showUrl = $derived(editor?.kind === 'bookmark' || (editor?.kind === 'edit' && !!node?.url));
	const heading = $derived(
		editor?.kind === 'move'
			? 'bookmarksMoveTitle'
			: editor?.kind === 'edit'
				? node?.url
					? 'bookmarksEditTitle'
					: 'bookmarksRenameTitle'
				: editor?.kind === 'folder'
					? 'bookmarksAddFolder'
					: 'bookmarksAddBookmark'
	);
	$effect(() => {
		if (!editor || previous === editor) return;
		previous = editor;
		title = node?.title ?? '';
		url = node?.url ?? '';
		destination = node?.parentId ?? model.moveDestinations[0]?.id ?? '';
	});
</script>

<Dialog.Root
	open={!!editor}
	onOpenChange={(open) => {
		if (!open) model.cancelEditor();
	}}
>
	<Dialog.Content
		showCloseButton={false}
		onEscapeKeydown={(event) => {
			if (model.busy) event.preventDefault();
		}}
		onInteractOutside={(event) => {
			if (model.busy) event.preventDefault();
		}}
	>
		<Dialog.Header>
			<Dialog.Title>{t(heading)}</Dialog.Title>
			<Dialog.Description>
				{editor?.kind === 'move' ? t('bookmarksMoveDescription') : t('bookmarksEditorDescription')}
			</Dialog.Description>
		</Dialog.Header>
		<form
			onsubmit={(event) => {
				event.preventDefault();
				void model.saveEditor(title, url.trim(), destination);
			}}
		>
			{#if editor?.kind === 'move'}
				<div class="field">
					<Label for="bookmark-destination">{t('bookmarksDestination')}</Label>
					<NativeSelect.Root
						id="bookmark-destination"
						class="w-full"
						bind:value={destination}
						disabled={model.busy}
						required
					>
						{#each model.moveDestinations as folder (folder.id)}
							<NativeSelect.Option value={folder.id}>{model.folderPath(folder)}</NativeSelect.Option
							>
						{/each}
					</NativeSelect.Root>
				</div>
			{:else}
				<div class="field">
					<Label for="bookmark-name">{t('bookmarksName')}</Label>
					<Input
						id="bookmark-name"
						bind:value={title}
						maxlength={512000}
						disabled={model.busy}
						autocomplete="off"
					/>
				</div>
				{#if showUrl}
					<div class="field">
						<Label for="bookmark-url">{t('bookmarksAddress')}</Label>
						<Input
							id="bookmark-url"
							bind:value={url}
							maxlength={512000}
							disabled={model.busy}
							required
							autocomplete="off"
							spellcheck={false}
						/>
					</div>
				{/if}
			{/if}
			{#if model.editorError}<p role="alert" class="error">{model.editorError}</p>{/if}
			<Dialog.Footer>
				<Button variant="ghost" disabled={model.busy} onclick={() => model.cancelEditor()}
					>{t('cancel')}</Button
				>
				<Button type="submit" disabled={model.busy || (editor?.kind === 'move' && !destination)}>
					{t(
						model.busy
							? 'bookmarksSaving'
							: editor?.kind === 'move'
								? 'bookmarksMoveConfirm'
								: 'bookmarksSave'
					)}
				</Button>
			</Dialog.Footer>
		</form>
	</Dialog.Content>
</Dialog.Root>

<style>
	@layer components {
		form {
			display: grid;
			gap: 16px;
		}
		.field {
			display: grid;
			gap: 7px;
		}
		.error {
			color: var(--destructive);
			font-size: 12px;
			margin: 0;
		}
	}
</style>
