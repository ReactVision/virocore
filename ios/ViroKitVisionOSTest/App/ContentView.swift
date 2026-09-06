import SwiftUI

struct ContentView: View {
    @Environment(\.openImmersiveSpace)    var openImmersiveSpace
    @Environment(\.dismissImmersiveSpace) var dismissImmersiveSpace
    @State private var isImmersive = false

    var body: some View {
        VStack(spacing: 24) {
            Text("ViroKit visionOS Test")
                .font(.extraLargeTitle)
            Text("Red box scene — 30 cm cube at (0, −0.5, −1.5) m")
                .foregroundStyle(.secondary)
            Button(isImmersive ? "Exit Immersive" : "Launch Immersive") {
                Task {
                    if isImmersive {
                        await dismissImmersiveSpace()
                        isImmersive = false
                    } else {
                        await openImmersiveSpace(id: ViroImmersiveSpace.id)
                        isImmersive = true
                    }
                }
            }
            .buttonStyle(.borderedProminent)
        }
        .padding(40)
        .task {
            // Auto-open for diagnostic builds — remove before shipping.
            try? await Task.sleep(for: .seconds(2))
            await openImmersiveSpace(id: ViroImmersiveSpace.id)
            isImmersive = true
        }
    }
}
