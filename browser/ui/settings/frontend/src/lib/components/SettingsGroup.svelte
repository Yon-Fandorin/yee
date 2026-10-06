<script lang="ts">
	import type { Snippet } from 'svelte';
	import * as Card from '#lib/components/ui/card/index.ts';
	let {
		title,
		description,
		children,
		actions
	}: {
		title: string;
		description?: string;
		children: Snippet;
		actions?: Snippet;
	} = $props();
</script>

<section class="settings-section">
	<div class="section-heading">
		<div>
			<h2>{title}</h2>
			{#if description}<p class="section-description">{description}</p>{/if}
		</div>
		{#if actions}<div class="section-actions">{@render actions()}</div>{/if}
	</div>
	<Card.Root class="settings-group gap-0 overflow-hidden rounded-lg py-0 shadow-none">
		<Card.Content class="p-0">{@render children()}</Card.Content>
	</Card.Root>
</section>

<style>
	@layer components {
		.settings-section {
			margin-top: 30px;
		}
		.section-heading {
			display: flex;
			align-items: center;
			justify-content: space-between;
			gap: 20px;
			margin-bottom: 12px;
		}
		h2 {
			margin: 0;
			font-size: 13px;
			font-weight: 550;
			letter-spacing: -0.1px;
		}
		.section-description {
			color: var(--muted-foreground);
			font-size: 12px;
			margin-top: 4px;
			max-width: 65ch;
		}
		@media (max-width: 780px) {
			.section-heading {
				flex-wrap: wrap;
				gap: 10px;
			}
		}
	}
</style>
