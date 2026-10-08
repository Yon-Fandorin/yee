<script lang="ts">
	import type { Snippet } from 'svelte';
	let {
		id,
		message,
		error = false,
		role,
		actions
	}: {
		id?: string;
		message: string;
		error?: boolean;
		role?: 'alert';
		actions?: Snippet;
	} = $props();
</script>

<div {id} {role} class="notice" class:error>
	<p>{message}</p>
	{@render actions?.()}
</div>

<style>
	@layer components {
		.notice {
			display: flex;
			align-items: center;
			justify-content: space-between;
			gap: 16px;
			margin-top: 14px;
			padding: 12px 14px;
			border: 1px solid var(--border);
			border-radius: 6px;
			background: var(--card);
			font-size: 12px;
			color: var(--muted-foreground);
		}
		.error {
			color: var(--destructive);
		}
	}
</style>
