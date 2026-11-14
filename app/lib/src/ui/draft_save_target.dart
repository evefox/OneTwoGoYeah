import '../infrastructure/local_project_store.dart';

/// The draft a new save operation will target.
final class DraftSaveTarget {
  const DraftSaveTarget({this.existingId, required this.needsConfirmation});

  /// Identifier of the draft that already uses the requested name.
  final String? existingId;

  /// Whether the user must confirm overwriting that draft first.
  final bool needsConfirmation;
}

/// Resolves the target for the new-draft save flow.
///
/// Any draft already using the requested name requires confirmation, including
/// the draft currently loaded in the workspace: saving it again under the same
/// name through "save as" must not create a same-name duplicate.
DraftSaveTarget resolveDraftSaveTarget(DraftSummary? existing) =>
    DraftSaveTarget(
      existingId: existing?.id,
      needsConfirmation: existing != null,
    );
