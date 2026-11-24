import SwiftUI

/// Slot manager UI. Works entirely with our own DK2S container.
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
            Rectangle().fill(Color.black.opacity(0.88)).ignoresSafeArea()
            VStack(spacing: 12) {
                Text("Save / Load").font(.system(size: 24, weight: .bold, design: .serif))
                    .foregroundColor(Color(red: 0.95, green: 0.85, blue: 0.62))
                HStack {
                    Button("Save to selected") {
                        if selected != nil { showConfirm = true }
                    }.disabled(selected == nil)
                    Button("Load selected") {
                        if let s = selected { _ = DK2SaveManager.shared.load(slot: s, bridge: DK2Bridge.shared) }
                    }.disabled(selected == nil)
                    Button("Delete") {
                        if let s = selected {
                            _ = DK2SaveManager.shared.delete(slot: s)
                            refresh()
                        }
                    }.disabled(selected == nil)
                    Button("Close") { onClose() }
                }
                .foregroundColor(.white)

                List(slots, id: \.slot) { slot in
                    Button {
                        selected = slot.slot
                    } label: {
                        HStack {
                            Image(systemName: slot.exists ? (slot.valid ? "externaldrive.fill" : "exclamationmark.triangle") : "square")
                            VStack(alignment: .leading) {
                                Text(slot.slot.uppercased()).font(.headline)
                                Text(slot.exists
                                     ? "\(slot.mission.isEmpty ? "unknown" : slot.mission) · tick \(slot.tick) · \(slot.creatures) creatures"
                                     : "Empty slot")
                                    .font(.caption)
                            }
                            Spacer()
                            if slot.slot == DK2SaveManager.autoSlot { Text("AUTO").font(.caption2) }
                        }
                    }
                    .foregroundColor(selected == slot.slot ? Color.yellow : Color.white)
                }
                .onAppear { refresh() }
            }
            .padding()
            .alert("Overwrite slot?", isPresented: $showConfirm) {
                Button("Cancel", role: .cancel) {}
                Button("Save") {
                    if let s = selected {
                        _ = DK2SaveManager.shared.save(slot: s, bridge: DK2Bridge.shared)
                        refresh()
                    }
                }
            }
        }
    }
}
