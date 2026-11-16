use std::ptr;
unsafe extern "C" {fn ores_iot_required_capacity(kind:i32, payload_len:usize,required:*mut usize)->i32;}
#[test] fn frame_capacity(){for kind in 1..=3 {for n in [0,1,1023,1024]{let mut got=usize::MAX;assert_eq!(unsafe{ores_iot_required_capacity(kind,n,&mut got)},0);assert_eq!(got,8+n);}}let mut got=77;assert_eq!(unsafe{ores_iot_required_capacity(1,1025,&mut got)},2);assert_eq!(got,0);got=77;assert_eq!(unsafe{ores_iot_required_capacity(0,1,&mut got)},6);assert_eq!(got,0);assert_eq!(unsafe{ores_iot_required_capacity(1,0,ptr::null_mut())},1);}

#[repr(C)]
struct FrameView { kind: i32, payload: *const u8, length: usize }
unsafe extern "C" {
    fn ores_iot_encode(kind: i32, payload: *const u8, length: usize,
        dest: *mut u8, capacity: usize, written: *mut usize) -> i32;
    fn ores_iot_decode(frame: *const u8, length: usize, view: *mut FrameView) -> i32;
}
#[test]
fn size_plan_matches_real_encoder_for_entire_payload_domain() {
    for kind in 1..=3 {
        for length in 0usize..=1024 {
            let payload: Vec<u8> = (0..length).map(|i| ((i * 17 + 3) % 251) as u8).collect();
            let mut need = usize::MAX;
            assert_eq!(unsafe { ores_iot_required_capacity(kind, length, &mut need) }, 0);
            let mut frame = vec![0xa5u8; need];
            let mut written = usize::MAX;
            assert_eq!(unsafe { ores_iot_encode(kind, payload.as_ptr(), length,
                frame.as_mut_ptr(), frame.len(), &mut written) }, 0);
            assert_eq!(written, need);
            let mut view = FrameView { kind: 0, payload: ptr::null(), length: 0 };
            assert_eq!(unsafe { ores_iot_decode(frame.as_ptr(), written, &mut view) }, 0);
            assert_eq!(view.kind, kind);
            assert_eq!(view.length, length);
            assert_eq!(unsafe { std::slice::from_raw_parts(view.payload, length) }, payload);
            frame.fill(0x5a);
            written = usize::MAX;
            assert_eq!(unsafe { ores_iot_encode(kind, payload.as_ptr(), length,
                frame.as_mut_ptr(), need - 1, &mut written) }, 3);
            assert_eq!(written, 0);
            assert!(frame.iter().all(|byte| *byte == 0x5a));
        }
    }
}
