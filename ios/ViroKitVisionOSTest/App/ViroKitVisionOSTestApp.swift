import SwiftUI

@main
struct ViroKitVisionOSTestApp: App {
    @State private var immersionStyle: ImmersionStyle = .full

    var body: some Scene {
        WindowGroup {
            ContentView()
                .viroImmersiveSpaceController()
        }

        ImmersiveSpace(id: ViroImmersiveSpace.id) {
            ViroImmersiveSpaceView()
        }
        .immersionStyle(selection: $immersionStyle, in: .mixed, .full)
    }
}
