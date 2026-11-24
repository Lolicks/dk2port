import SwiftUI

struct DK2SaveSlotsView: View {
    @State private var slots: [DK2SaveManager.SlotInfo] = []
    @State private var selected: String?
    @State private var showConfirm = false
    let onClose: () -> Void

    private func refresh() {
        slots = DK2SaveManager.shared.allSlots()
    }

    var body: some View {
        ZStack {
            Color.black.opacity(0.9).ignoresSafeArea()
            VStack(spacing: 12) {
                DK2Text(text: DK2L10n.shared[145], style: .subTitle)
                    .accessibilityIdentifier("loadGame")
                HStack(spacing: 12) {
                    DK2UIStyle.okButton("Save") {
                        if selected != nil { showConfirm = true }
                    }
                    DK2UIStyle.okButton("Load") {
                        if let slotName = selected {
                            _ = DK2SaveManager.shared.load(slot: slotName, bridge: DK2Bridge.shared())
                        }
                    }
                    DK2UIStyle.cancelButton("Delete") {
                        if let slotName = selected {
                            _ = DK2SaveManager.shared.delete(slot: slotName)
                            refresh()
                        }
                    }
                    DK2UIStyle.cancelButton { onClose() }
                }
                List(slots, id: \.slot) { slot in
                    Button {
                        selected = slot.slot
                    } label: {
                        HStack {
                            Image(systemName: slot.exists
                                  ? (slot.valid ? "externaldrive.fill" : "exclamationmark.triangle")
                                  : "square")
                            VStack(alignment: .leading) {
                                Text(slot.slot.uppercased()).font(.headline)
                                Text(slot.exists
                                     ? "\(slot.mission.isEmpty ? "unknown" : slot.mission) - tick \(slot.tick) - \(slot.creatures) creatures"
                                     : "Empty slot")
                                    .font(.caption)
                            }
                            Spacer()
                            if slot.slot == DK2SaveManager.autoSlot {
                                Text("AUTO").font(.caption2)
                            }
                        }
                    }
                    .foregroundColor(selected == slot.slot ? DK2UIStyle.gold : DK2UIStyle.parchment)
                }
                .onAppear { refresh() }
            }
            .padding()
            .alert("Overwrite slot?", isPresented: $showConfirm) {
                Button("Cancel", role: .cancel) {}
                Button("Save") {
                    if let slotName = selected {
                        _ = DK2SaveManager.shared.save(slot: slotName, bridge: DK2Bridge.shared())
                        refresh()
                    }
                }
            }
        }
    }
}
