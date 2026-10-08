<script lang="ts">
	import SettingsNotice from './SettingsNotice.svelte';
	let {
		id,
		message,
		error = false,
		inset = false
	}: {
		id: string;
		message: string;
		error?: boolean;
		inset?: boolean;
	} = $props();
</script>

<div
	class:inset={inset && Boolean(message)}
	role={error ? 'alert' : 'status'}
	aria-live={error ? 'assertive' : 'polite'}
	aria-atomic="true"
>
	{#if message}<SettingsNotice {id} {message} {error} />{/if}
</div>

<style>
	@layer components {
		.inset {
			padding: 0 16px 16px;
		}
	}
</style>
