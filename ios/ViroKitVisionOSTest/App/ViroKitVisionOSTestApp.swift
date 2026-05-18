import SwiftUI

@main
struct ViroKitVisionOSTestApp: App {
    // DIAG: .full immersion — no passthrough compositing, GPU output fills the
    // entire display. If green appears here but not in .mixed, the issue is
    // mixed-mode compositor alpha/depth treatment.
    @State private var immersionStyle: ImmersionStyle = .full

    var body: some Scene {
        WindowGroup {
            ContentView()
                .viroImmersiveSpaceController()
        }

        ImmersiveSpace(id: ViroImmersiveSpace.id) {
            ViroImmersiveSpaceView()
        }
        .immersionStyle(selection: $immersionStyle, in: .full)
    }
}
