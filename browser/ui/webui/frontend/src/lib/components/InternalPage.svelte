<script lang="ts">
	import type { Snippet } from 'svelte';
	let {
		title,
		titleId,
		icon,
		actions,
		toolbar,
		children
	}: {
		title: string;
		titleId: string;
		icon?: Snippet;
		actions?: Snippet;
		toolbar?: Snippet;
		children: Snippet;
	} = $props();
</script>

<main aria-labelledby={titleId}>
	<div class="page">
		<header>
			<div class="heading">
				{@render icon?.()}
				<h1 id={titleId}>{title}</h1>
			</div>
			{#if actions}<div class="header-actions">{@render actions()}</div>{/if}
		</header>
		{#if toolbar}<div class="toolbar">{@render toolbar()}</div>{/if}
		{@render children()}
	</div>
</main>

<style>
	@layer components {
		main {
			height: 100dvh;
			overflow-y: auto;
			overscroll-behavior: contain;
		}
		.page {
			max-width: 960px;
			margin: 0 auto;
			padding: 38px 36px 64px;
		}
		header {
			display: flex;
			justify-content: space-between;
			align-items: center;
			gap: 20px;
			margin-bottom: 25px;
		}
		.heading,
		.header-actions {
			display: flex;
			align-items: center;
			gap: 10px;
		}
		.heading :global(svg) {
			color: var(--muted-foreground);
		}
		h1 {
			font-size: 18px;
			font-weight: 550;
			letter-spacing: -0.4px;
			margin: 0;
		}
		.toolbar {
			display: flex;
			align-items: center;
			justify-content: space-between;
			gap: 12px;
			padding-bottom: 14px;
			border-bottom: 1px solid var(--border);
		}
		@media (max-width: 620px) {
			.page {
				padding: 26px 20px 48px;
			}
			header {
				align-items: flex-start;
				flex-wrap: wrap;
				gap: 14px;
			}
			.toolbar {
				flex-wrap: wrap;
			}
		}
	}
</style>
